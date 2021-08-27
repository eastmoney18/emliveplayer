/*
 * IJKSDLGLView.m
 *
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * based on https://github.com/kolyvan/kxmovie
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#import "IJKSDLGLView.h"
#include "ijksdl/ijksdl_timer.h"
#include "ijksdl/ios/ijksdl_ios.h"
#include "ijksdl/ijksdl_gles2.h"
#import "IJKSDLHudViewController.h"

@interface IJKSDLGLView()
@property(atomic,strong) NSRecursiveLock *glActiveLock;
@property(atomic) BOOL glActivePaused;
@property(atomic) uint8_t *capturedFrame;
@end

@implementation IJKSDLGLView {
    EAGLContext     *_context;
    GLuint          _framebuffer;
    GLuint          _renderbuffer;
    GLint           _backingWidth;
    GLint           _backingHeight;
    GLint           _captureWidth;
    GLint           _captureHeight;
    BOOL            _capturing;
    int             _frameCount;
    
    int64_t         _lastFrameTime;

    IJK_GLES2_Renderer *_renderer;
    int                 _rendererGravity;
    IJK_Rotate_Mode      _rotateMode;
    BOOL            _isRenderBufferInvalidated;

    int             _tryLockErrorCount;
    BOOL            _didSetupGL;
    BOOL            _didStopGL;
    BOOL            _didLockedDueToMovedToWindow;
    BOOL            _shouldLockWhileBeingMovedToWindow;
    NSMutableArray *_registeredNotifications;

    IJKSDLHudViewController *_hudViewController;
}

+ (Class) layerClass
{
	return [CAEAGLLayer class];
}

- (id) initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _tryLockErrorCount = 0;
        _shouldLockWhileBeingMovedToWindow = YES;
        self.glActiveLock = [[NSRecursiveLock alloc] init];
        _registeredNotifications = [[NSMutableArray alloc] init];
        [self registerApplicationObservers];

        _didSetupGL = NO;
        [self setupGLOnce];

        _hudViewController = [[IJKSDLHudViewController alloc] init];
        [self addSubview:_hudViewController.tableView];
    }

    return self;
}

- (void)willMoveToWindow:(UIWindow *)newWindow
{
    if (!_shouldLockWhileBeingMovedToWindow) {
        [super willMoveToWindow:newWindow];
        return;
    }
    if (newWindow && !_didLockedDueToMovedToWindow) {
        [self lockGLActive];
        _didLockedDueToMovedToWindow = YES;
    }
    [super willMoveToWindow:newWindow];
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    if (self.window && _didLockedDueToMovedToWindow) {
        [self unlockGLActive];
        _didLockedDueToMovedToWindow = NO;
    }
}

- (BOOL)setupFBO:(GLuint *)framebuffer :(GLuint *)texture
{
    int width = _captureWidth;
    int height = _captureHeight;
    if (width <= 0) {
        width = _backingWidth;
        _captureWidth = width;
    }
    if (height <= 0) {
        height = _backingHeight;
        _captureHeight = height;
    }
    glGenFramebuffers(1, framebuffer);
    glGenTextures(1, texture);
    glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    GLenum err = glCheckFramebufferStatus(GL_RENDERBUFFER);
    if (err != GL_NO_ERROR) {
        ALOGI("gen fbo failed.");
    }
    return YES;
}

- (void)destroyFBO:(GLuint)framebuffer :(GLuint)texture
{
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &framebuffer);
}

- (BOOL)setupEAGLContext:(EAGLContext *)context
{
    glGenFramebuffers(1, &_framebuffer);
    glGenRenderbuffers(1, &_renderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _renderbuffer);
    [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer*)self.layer];
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_backingWidth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_backingHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _renderbuffer);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ALOGE("failed to make complete framebuffer object %x\n", status);
        return NO;
    }

    GLenum glError = glGetError();
    if (GL_NO_ERROR != glError) {
        ALOGE("failed to setup GL %x\n", glError);
        return NO;
    }

    return YES;
}

- (CAEAGLLayer *)eaglLayer
{
    return (CAEAGLLayer*) self.layer;
}

- (BOOL)setupGL
{
    if (_didSetupGL)
        return YES;

    if ([self isApplicationActive] == NO)
        return NO;

    CAEAGLLayer *eaglLayer = (CAEAGLLayer*) self.layer;
    eaglLayer.opaque = YES;
    eaglLayer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [NSNumber numberWithBool:NO], kEAGLDrawablePropertyRetainedBacking,
                                    kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat,
                                    nil];

    _scaleFactor = [[UIScreen mainScreen] scale];
    if (_scaleFactor < 0.1f)
        _scaleFactor = 1.0f;

    [eaglLayer setContentsScale:_scaleFactor];

    _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    if (_context == nil) {
        ALOGE("failed to setup EAGLContext\n");
        return NO;
    }

    EAGLContext *prevContext = [EAGLContext currentContext];
    [EAGLContext setCurrentContext:_context];

    _didSetupGL = NO;
    if ([self setupEAGLContext:_context]) {
        ALOGI("OK setup GL\n");
        _didSetupGL = YES;
    }

    [EAGLContext setCurrentContext:prevContext];
    return _didSetupGL;
}

- (BOOL)setupGLOnce
{
    if (_didSetupGL)
        return YES;

    if ([self isApplicationActive] == NO)
        return NO;

    if (![self tryLockGLActive])
        return NO;

    BOOL didSetupGL = [self setupGL];
    [self unlockGLActive];
    return didSetupGL;
}

- (BOOL)isApplicationActive
{
    UIApplicationState appState = [UIApplication sharedApplication].applicationState;
    switch (appState) {
        case UIApplicationStateActive:
            return YES;
        case UIApplicationStateInactive:
        case UIApplicationStateBackground:
        default:
            return NO;
    }
}

- (void)dealloc
{
    [self lockGLActive];

    _didStopGL = YES;

    EAGLContext *prevContext = [EAGLContext currentContext];
    [EAGLContext setCurrentContext:_context];
    
    IJK_GLES2_Renderer_reset(_renderer);
    IJK_GLES2_Renderer_freeP(&_renderer);

    if (_framebuffer) {
        glDeleteFramebuffers(1, &_framebuffer);
        _framebuffer = 0;
    }

    if (_renderbuffer) {
        glDeleteRenderbuffers(1, &_renderbuffer);
        _renderbuffer = 0;
    }

    glFinish();

    [EAGLContext setCurrentContext:prevContext];

    _context = nil;

    [self unregisterApplicationObservers];

    [self unlockGLActive];
}

- (void)setScaleFactor:(CGFloat)scaleFactor
{
    _scaleFactor = scaleFactor;
    [self invalidateRenderBuffer];
}

- (void)layoutSubviews
{
    [super layoutSubviews];

    CGRect selfFrame = self.frame;
    CGRect newFrame  = selfFrame;

    newFrame.size.width   = selfFrame.size.width * 1 / 3;
    newFrame.origin.x     = selfFrame.size.width * 2 / 3;

    newFrame.size.height  = selfFrame.size.height * 8 / 8;
    newFrame.origin.y    += selfFrame.size.height * 0 / 8;

    _hudViewController.tableView.frame = newFrame;
    [self invalidateRenderBuffer];
}

- (void)setContentMode:(UIViewContentMode)contentMode
{
    [super setContentMode:contentMode];

    switch (contentMode) {
        case UIViewContentModeScaleToFill:
            _rendererGravity = IJK_GLES2_GRAVITY_RESIZE;
            break;
        case UIViewContentModeScaleAspectFit:
            _rendererGravity = IJK_GLES2_GRAVITY_RESIZE_ASPECT;
            break;
        case UIViewContentModeScaleAspectFill:
            _rendererGravity = IJK_GLES2_GRAVITY_RESIZE_ASPECT_FILL;
            break;
        default:
            _rendererGravity = IJK_GLES2_GRAVITY_RESIZE_ASPECT;
            break;
    }
    [self invalidateRenderBuffer];
}

- (void)setupRotateMode:(int)degree
{
    IJK_Rotate_Mode rotateMode;
    switch (degree) {
        case 90:
            rotateMode = IJK_ROTATE_RIGHT;
            break;
        case 180:
            rotateMode = IJK_ROTATE_TOP_BOTTOM;
            break;
        case 270:
            rotateMode = IJK_ROTATE_LEFT;
            break;
        case 0:
            rotateMode = IJK_ROTATE_NORMAL;
            break;
        default:
            break;
    }

    _rotateMode = rotateMode;
    [self invalidateRenderBuffer];
}

- (UIImage *)captureFrame:(int)timeout :(int)width :(int)height
{
    UIImage *finalImage = nil;
    if (_capturing) {
        ALOGI("capture frame now, please wait finished.");
        return nil;
    }
    int i = 0;
    _capturing = YES;
    _captureWidth = width;
    _captureHeight = height;
    for (i = 0; i < timeout*10 && !_capturedFrame; i++) {
        usleep(100*1000);
    }
    if (_capturedFrame) {
        uint32_t tmp = 0;
        uint32_t *frame = (uint32_t *)_capturedFrame;
        for (i = 0; i < _captureHeight / 2; i++) {
            for (int j = 0; j < _captureWidth; j++) {
                int idx = i * _captureWidth + j;
                int idx1 = (_captureHeight - i - 1) * _captureWidth + j;
                tmp = frame[idx];
                frame[idx] = frame[idx1];
                frame[idx1] = tmp;
            }
        }
        NSData *data = [NSData dataWithBytes:_capturedFrame length:_captureWidth*_captureHeight*4];
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider = CGDataProviderCreateWithCFData((CFDataRef)data);
        CGImageRef imgRef = CGImageCreate(_captureWidth, _captureHeight, 8, 32, _captureWidth*4, colorSpace, kCGImageAlphaLast | kCGBitmapByteOrder32Big, provider, NULL, false, kCGRenderingIntentDefault);
        if (imgRef) {
            
            finalImage = [UIImage imageWithCGImage:imgRef];
            CGImageRelease(imgRef);
        }
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(colorSpace);
        free(_capturedFrame);
        _capturedFrame = nil;
    }
    _capturing = NO;
    return finalImage;
}

- (BOOL)setupRenderer: (SDL_VoutOverlay *) overlay
{
    if (overlay == nil)
        return _renderer != nil;

    if (!IJK_GLES2_Renderer_isValid(_renderer) ||
        !IJK_GLES2_Renderer_isFormat(_renderer, overlay->format)) {

        IJK_GLES2_Renderer_reset(_renderer);
        IJK_GLES2_Renderer_freeP(&_renderer);

        _renderer = IJK_GLES2_Renderer_create(overlay);
        if (!IJK_GLES2_Renderer_isValid(_renderer))
            return NO;

        if (!IJK_GLES2_Renderer_use(_renderer))
            return NO;

        IJK_GLES2_Renderer_setGravity(_renderer, _rendererGravity, _backingWidth, _backingHeight);
    }

    return YES;
}

- (void)invalidateRenderBuffer
{
    ALOGI("invalidateRenderBuffer\n");
    [self lockGLActive];

    _isRenderBufferInvalidated = YES;

    if ([[NSThread currentThread] isMainThread]) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
            if (_isRenderBufferInvalidated)
                [self display:nil];
        });
    } else {
        [self display:nil];
    }

    [self unlockGLActive];
}

- (void)display: (SDL_VoutOverlay *) overlay
{
    if (![self setupGLOnce])
        return;

    if (![self tryLockGLActive]) {
        if (0 == (_tryLockErrorCount % 100)) {
            ALOGW("IJKSDLGLView:display: unable to tryLock GL active: %d\n", _tryLockErrorCount);
        }
        _tryLockErrorCount++;
        return;
    }

    _tryLockErrorCount = 0;
    if (_context && !_didStopGL) {
        EAGLContext *prevContext = [EAGLContext currentContext];
        [EAGLContext setCurrentContext:_context];
        [self displayInternal:overlay];
        [EAGLContext setCurrentContext:prevContext];
    }

    [self unlockGLActive];
}

- (void)renderInternal:(SDL_VoutOverlay *)overlay
{
    if (_capturing && !_capturedFrame) {
        GLuint framebuffer, texture;
        if ([self setupFBO:&framebuffer :&texture]) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(0, 0, _captureWidth, _captureHeight);
            if (!IJK_GLES2_Renderer_renderOverlay(_renderer, overlay)) {
                ALOGE("[EGL] IJK_GLES2_render failed\n");
                return;
            }
            uint8_t *frame = (uint8_t *)malloc(_captureWidth*_captureHeight*4);
            if (frame) {
                glReadPixels(0, 0, _captureWidth, _captureHeight, GL_RGBA, GL_UNSIGNED_BYTE, frame);
                _capturedFrame = frame;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            [self destroyFBO:framebuffer :texture];
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glViewport(0, 0, _backingWidth, _backingHeight);
    
    if (!IJK_GLES2_Renderer_renderOverlay(_renderer, overlay))
        ALOGE("[EGL] IJK_GLES2_render failed\n");
    
    glBindRenderbuffer(GL_RENDERBUFFER, _renderbuffer);
    [_context presentRenderbuffer:GL_RENDERBUFFER];
}

// NOTE: overlay could be NULl
- (void)displayInternal: (SDL_VoutOverlay *) overlay
{
    if (![self setupRenderer:overlay]) {
        if (!overlay && !_renderer) {
            ALOGW("IJKSDLGLView: setupDisplay not ready\n");
        } else {
            ALOGE("IJKSDLGLView: setupDisplay failed\n");
        }
        return;
    }

    [[self eaglLayer] setContentsScale:_scaleFactor];

    if (_isRenderBufferInvalidated) {
        ALOGI("IJKSDLGLView: renderbufferStorage fromDrawable\n");
        _isRenderBufferInvalidated = NO;

        glBindRenderbuffer(GL_RENDERBUFFER, _renderbuffer);
        [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer*)self.layer];
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_backingWidth);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_backingHeight);
        IJK_GLES2_Renderer_setGravity(_renderer, _rendererGravity, _backingWidth, _backingHeight);
        IJK_GLES2_Renderer_setRotate(_renderer, _rotateMode);
    }

    [self renderInternal:overlay];

    int64_t current = (int64_t)SDL_GetTickHR();
    int64_t delta   = (current > _lastFrameTime) ? current - _lastFrameTime : 0;
    if (delta <= 0) {
        _lastFrameTime = current;
    } else if (delta >= 1000) {
        _fps = ((CGFloat)_frameCount) * 1000 / delta;
        _frameCount = 0;
        _lastFrameTime = current;
    } else {
        _frameCount++;
    }
}

#pragma mark AppDelegate

- (void) lockGLActive
{
    [self.glActiveLock lock];
}

- (void) unlockGLActive
{
    [self.glActiveLock unlock];
}

- (BOOL) tryLockGLActive
{
    if (![self.glActiveLock tryLock])
        return NO;

    /*-
    if ([UIApplication sharedApplication].applicationState != UIApplicationStateActive &&
        [UIApplication sharedApplication].applicationState != UIApplicationStateInactive) {
        [self.appLock unlock];
        return NO;
    }
     */

    if (self.glActivePaused) {
        [self.glActiveLock unlock];
        return NO;
    }
    
    return YES;
}

- (void)toggleGLPaused:(BOOL)paused
{
    [self lockGLActive];
    if (!self.glActivePaused && paused) {
        if (_context != nil) {
            EAGLContext *prevContext = [EAGLContext currentContext];
            [EAGLContext setCurrentContext:_context];
            glFinish();
            [EAGLContext setCurrentContext:prevContext];
        }
    }
    self.glActivePaused = paused;
    [self unlockGLActive];
}

- (void)registerApplicationObservers
{

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillEnterForeground)
                                                 name:UIApplicationWillEnterForegroundNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationWillEnterForegroundNotification];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidBecomeActive)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationDidBecomeActiveNotification];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillResignActive)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationWillResignActiveNotification];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidEnterBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationDidEnterBackgroundNotification];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillTerminate)
                                                 name:UIApplicationWillTerminateNotification
                                               object:nil];
    [_registeredNotifications addObject:UIApplicationWillTerminateNotification];
}

- (void)unregisterApplicationObservers
{
    for (NSString *name in _registeredNotifications) {
        [[NSNotificationCenter defaultCenter] removeObserver:self
                                                        name:name
                                                      object:nil];
    }
}

- (void)applicationWillEnterForeground
{
    ALOGI("IJKSDLGLView:applicationWillEnterForeground: %d", (int)[UIApplication sharedApplication].applicationState);
    [self toggleGLPaused:NO];
}

- (void)applicationDidBecomeActive
{
    ALOGI("IJKSDLGLView:applicationDidBecomeActive: %d", (int)[UIApplication sharedApplication].applicationState);
    [self toggleGLPaused:NO];
}

- (void)applicationWillResignActive
{
    ALOGI("IJKSDLGLView:applicationWillResignActive: %d", (int)[UIApplication sharedApplication].applicationState);
    [self toggleGLPaused:YES];
}

- (void)applicationDidEnterBackground
{
    ALOGI("IJKSDLGLView:applicationDidEnterBackground: %d", (int)[UIApplication sharedApplication].applicationState);
    [self toggleGLPaused:YES];
}

- (void)applicationWillTerminate
{
    ALOGI("IJKSDLGLView:applicationWillTerminate: %d", (int)[UIApplication sharedApplication].applicationState);
    [self toggleGLPaused:YES];
}

#pragma mark snapshot

- (UIImage*)snapshot
{
    [self lockGLActive];

    UIImage *image = [self snapshotInternal];

    [self unlockGLActive];

    return image;
}

- (UIImage*)snapshotInternal
{
    if (isIOS7OrLater()) {
        return [self snapshotInternalOnIOS7AndLater];
    } else {
        return [self snapshotInternalOnIOS6AndBefore];
    }
}

- (UIImage*)snapshotInternalOnIOS7AndLater
{
    UIGraphicsBeginImageContextWithOptions(self.bounds.size, NO, 0.0);
    // Render our snapshot into the image context
    [self drawViewHierarchyInRect:self.bounds afterScreenUpdates:NO];

    // Grab the image from the context
    UIImage *complexViewImage = UIGraphicsGetImageFromCurrentImageContext();
    // Finish using the context
    UIGraphicsEndImageContext();

    return complexViewImage;
}

- (UIImage*)snapshotInternalOnIOS6AndBefore
{
    EAGLContext *prevContext = [EAGLContext currentContext];
    [EAGLContext setCurrentContext:_context];

    GLint backingWidth, backingHeight;

    // Bind the color renderbuffer used to render the OpenGL ES view
    // If your application only creates a single color renderbuffer which is already bound at this point,
    // this call is redundant, but it is needed if you're dealing with multiple renderbuffers.
    // Note, replace "viewRenderbuffer" with the actual name of the renderbuffer object defined in your class.
    glBindRenderbuffer(GL_RENDERBUFFER, _renderbuffer);

    // Get the size of the backing CAEAGLLayer
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &backingWidth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &backingHeight);

    NSInteger x = 0, y = 0, width = backingWidth, height = backingHeight;
    NSInteger dataLength = width * height * 4;
    GLubyte *data = (GLubyte*)malloc(dataLength * sizeof(GLubyte));

    // Read pixel data from the framebuffer
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels((int)x, (int)y, (int)width, (int)height, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Create a CGImage with the pixel data
    // If your OpenGL ES content is opaque, use kCGImageAlphaNoneSkipLast to ignore the alpha channel
    // otherwise, use kCGImageAlphaPremultipliedLast
    CGDataProviderRef ref = CGDataProviderCreateWithData(NULL, data, dataLength, NULL);
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGImageRef iref = CGImageCreate(width, height, 8, 32, width * 4, colorspace, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast,
                                    ref, NULL, true, kCGRenderingIntentDefault);

    [EAGLContext setCurrentContext:prevContext];

    // OpenGL ES measures data in PIXELS
    // Create a graphics context with the target size measured in POINTS
    UIGraphicsBeginImageContext(CGSizeMake(width, height));

    CGContextRef cgcontext = UIGraphicsGetCurrentContext();
    // UIKit coordinate system is upside down to GL/Quartz coordinate system
    // Flip the CGImage by rendering it to the flipped bitmap context
    // The size of the destination area is measured in POINTS
    CGContextSetBlendMode(cgcontext, kCGBlendModeCopy);
    CGContextDrawImage(cgcontext, CGRectMake(0.0, 0.0, width, height), iref);

    // Retrieve the UIImage from the current context
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    // Clean up
    free(data);
    CFRelease(ref);
    CFRelease(colorspace);
    CGImageRelease(iref);

    return image;
}

#pragma mark IJKFFHudController
- (void)setHudValue:(NSString *)value forKey:(NSString *)key
{
    if ([[NSThread currentThread] isMainThread]) {
        [_hudViewController setHudValue:value forKey:key];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self setHudValue:value forKey:key];
        });
    }
}

- (void)setShouldLockWhileBeingMovedToWindow:(BOOL)shouldLockWhileBeingMovedToWindow
{
    _shouldLockWhileBeingMovedToWindow = shouldLockWhileBeingMovedToWindow;
}

- (void)setShouldShowHudView:(BOOL)shouldShowHudView
{
    _hudViewController.tableView.hidden = !shouldShowHudView;
}

- (BOOL)shouldShowHudView
{
    return !_hudViewController.tableView.hidden;
}

@end
