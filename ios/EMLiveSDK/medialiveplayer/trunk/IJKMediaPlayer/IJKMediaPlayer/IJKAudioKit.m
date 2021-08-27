/*
 * IJKAudioKit.m
 *
 * Copyright (c) 2013-2014 Zhang Rui <bbcallen@gmail.com>
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

#import "IJKAudioKit.h"

@implementation IJKAudioKit {
    BOOL _audioSessionInitialized;
}

+ (IJKAudioKit *)sharedInstance
{
    static IJKAudioKit *sAudioKit = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sAudioKit = [[IJKAudioKit alloc] init];
    });
    return sAudioKit;
}

- (void)setupAudioSession
{
    if (!_audioSessionInitialized) {
//        [[NSNotificationCenter defaultCenter] addObserver: self
//                                                 selector: @selector(handleInterruption:)
//                                                     name: AVAudioSessionInterruptionNotification
//                                                   object: [AVAudioSession sharedInstance]];
        _audioSessionInitialized = YES;
    }

    /* Set audio session to mediaplayback */
    NSError *error = nil;
    AVAudioSession *session = [AVAudioSession sharedInstance];
    BOOL setActive = NO;
    if (![[session category] isEqualToString:AVAudioSessionCategoryPlayback] && ![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
        if (NO == [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback withOptions:AVAudioSessionCategoryOptionMixWithOthers error:&error]) {
            NSLog(@"IJKAudioKit: AVAudioSession.setCategory() failed: %@\n", error ? [error localizedDescription] : @"nil");
            return;
        }
        setActive = YES;
    }
    error = nil;
    AVAudioSessionPortDescription *routePort = session.currentRoute.outputs.firstObject;
    NSString *portType = routePort.portType;
    if ([portType isEqualToString:AVAudioSessionPortBuiltInReceiver]) {
        [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:&error];
        setActive = YES;
    }
    if (setActive && NO == [session setActive:YES error:&error]) {
        NSLog(@"IJKAudioKit: AVAudioSession.setActive(YES) failed: %@\n", error ? [error localizedDescription] : @"nil");
        return;
    }

    return ;
}


- (BOOL)setActive:(BOOL)active
{
    NSError *err;
    if (active != NO) {
        [[AVAudioSession sharedInstance] setActive:YES error:&err];
    } else {
        @try {
            //[[AVAudioSession sharedInstance] setActive:NO error:nil];
            [[AVAudioSession sharedInstance] setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:&err];
        } @catch (NSException *exception) {
            NSLog(@"failed to inactive AVAudioSession\n");
        }
    }
    if (err != nil) {
        NSLog(@"set audio sessio failed:%d", (int)active);
    }
}

- (void)handleInterruption:(NSNotification *)notification
{
    int reason = [[[notification userInfo] valueForKey:AVAudioSessionInterruptionTypeKey] intValue];
    switch (reason) {
        case AVAudioSessionInterruptionTypeBegan: {
            NSLog(@"AVAudioSessionInterruptionTypeBegan\n");
            //[self setActive:NO];
            break;
        }
        case AVAudioSessionInterruptionTypeEnded: {
            NSLog(@"AVAudioSessionInterruptionTypeEnded\n");
            [self setActive:YES];
            break;
        }
    }
}


- (void)dealloc
{
    NSLog(@"IJKAudioKit dealloc");
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
