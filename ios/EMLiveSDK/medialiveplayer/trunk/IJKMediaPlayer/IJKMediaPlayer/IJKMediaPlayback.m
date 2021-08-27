/*
 * IJKMediaPlayback.m
 *
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
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

#import "IJKMediaPlayback.h"

// Video
NSString *const EMMPMediaPlaybackIsPreparedToPlayDidChangeNotification = @"EMMPMediaPlaybackIsPreparedToPlayDidChangeNotification";
NSString *const EMMPMediaPlaybackPreparedToPlayNotification = @"EMMPMediaPlaybackPreparedToPlayNotification";

NSString *const EMMPMoviePlayerPlaybackDidFinishNotification = @"EMMPMoviePlayerPlaybackDidFinishNotification";
NSString *const EMMPMoviePlayerPlaybackDidFinishReasonUserInfoKey =
    @"EMMPMoviePlayerPlaybackDidFinishReasonUserInfoKey";
NSString *const EMMPMoviePlayerPlaybackStateDidChangeNotification = @"EMMPMoviePlayerPlaybackStateDidChangeNotification";
NSString *const EMMPMoviePlayerLoadStateDidChangeNotification = @"EMMPMoviePlayerLoadStateDidChangeNotification";

NSString *const EMMPMoviePlayerIsAirPlayVideoActiveDidChangeNotification = @"EMMPMoviePlayerIsAirPlayVideoActiveDidChangeNotification";

NSString *const EMMPMovieNaturalSizeAvailableNotification = @"EMMPMovieNaturalSizeAvailableNotification";

NSString *const EMMPMoviePlayerVideoDecoderOpenNotification = @"EMMPMoviePlayerVideoDecoderOpenNotification";

NSString *const EMMPMoviePlayerFirstVideoFrameRenderedNotification = @"EMMPMoviePlayerFirstVideoFrameRenderedNotification";
NSString *const EMMPMoviePlayerFirstAudioFrameRenderedNotification = @"EMMPMoviePlayerFirstAudioFrameRenderedNotification";

NSString *const EMMPMoviePlayerDidSeekCompleteNotification = @"EMMPMoviePlayerDidSeekCompleteNotification";
NSString *const EMMPMoviePlayerDidSeekCompleteTargetKey = @"EMMPMoviePlayerDidSeekCompleteTargetKey";
NSString *const EMMPMoviePlayerDidSeekCompleteErrorKey = @"EMMPMoviePlayerDidSeekCompleteErrorKey";

NSString *const IJKMPMoviePlayerOpenHWVideoDecoderFailedNotification = @"IJKMPMoviePlayerOpenHWVideoDecoderFailedNotification";
NSString *const IJKMPMoviePlayerConnectSeverSuccessNotification = @"IJKMPMoviePlayerConnectSeverSuccessNotification";

NSString *const IJKMPMoviePlayerPlayProgressNotification = @"IJKMPMoviePlayerPlayProgressNotification";
NSString *const IJKMPMoviePlayerPlayStreamUnixTimeNotification = @"IJKMPMoviePlayerPlayStreamUnixTimeNotification";
NSString *const IJKMPMoviePlayerNetDisconnectNotification = @"IJKMPMoviePlayerNetDisconnectNotification";
NSString *const IJKMPMoviePlayerNetReconnectNotification = @"IJKMPMoviePlayerNetReconnectNotification";
NSString *const IJKMPMoviePlayerNetForbiddenNotification = @"IJKMPMoviePlayerNetForbiddenNotification";

// Voice
NSString *const IJKAVMoviePlayerIsPreparedToPlayDidChangeNotification = @"IJKAVMoviePlayerIsPreparedToPlayDidChangeNotification";
NSString *const IJKAVMoviePlayerLoadedTimeNotification = @"IJKAVMoviePlayerLoadedTimeNotification";
NSString *const IJKAVMoviePlayerControllerPlayVoiceProgress = @"IJKAVMoviePlayerControllerPlayVoiceProgress";
NSString *const IJKAVMoviePlayerControllerPlayVoiceFailed = @"IJKAVMoviePlayerControllerPlayVoiceFailed";
NSString *const IJKAVMoviePlayerPlaybackDidFinishNotification = @"IJKAVMoviePlayerPlaybackDidFinishNotification";

@implementation IJKMediaUrlOpenData {
    NSString *_url;
    BOOL _handled;
    BOOL _urlChanged;
}

- (id)initWithUrl:(NSString *)url
            event:(IJKMediaEvent)event
     segmentIndex:(int)segmentIndex
     retryCounter:(int)retryCounter
{
    self = [super init];
    if (self) {
        self->_url          = url;
        self->_event        = event;
        self->_segmentIndex = segmentIndex;
        self->_retryCounter = retryCounter;

        self->_error        = 0;
        self->_handled      = NO;
        self->_urlChanged   = NO;
    }
    return self;
}

- (void)setHandled:(BOOL)handled
{
    _handled = handled;
}

- (BOOL)isHandled
{
    return _handled;
}

- (BOOL)isUrlChanged
{
    return _urlChanged;
}

- (NSString *)url
{
    return _url;
}

- (void)setUrl:(NSString *)url
{
    assert(url);

    _handled = YES;

    if (url == _url)
        return;

    if ([self.url compare:url]) {
        _urlChanged = YES;
        _url = url;
    }
}

@end
