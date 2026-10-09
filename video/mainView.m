// NeonClockView.m — Video screensaver (Fireplace/Fishtank) for macOS
#import <AppKit/AppKit.h>
#import <stdlib.h>
#import <CoreMedia/CoreMedia.h>
#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVKit.h>

// ---------------------------------------------------------------------------
// Minimal declaration of the ScreenSaver API we need.
// ---------------------------------------------------------------------------
@interface ScreenSaverView : NSView
- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview;
- (void)startAnimation;
- (void)stopAnimation;
- (void)animateOneFrame;
- (BOOL)hasConfigureSheet;
- (NSWindow *)configureSheet;
@end

@interface NeonClockView : ScreenSaverView {
@private
    AVPlayer *_player;
    AVPlayerView *_playerView;
}
@end

@implementation NeonClockView

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview {
    self = [super initWithFrame:frame isPreview:isPreview];
    if (self) {
        [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    }
    return self;
}

- (void)startAnimation {
    [super startAnimation];
    if (_player) return;

    // Look for fireplace.mp4 or fishtank.mp4 inside the screensaver bundle
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSURL *fireplaceURL = [bundle URLForResource:@"fireplace" withExtension:@"mp4"];
    NSURL *fishtankURL  = [bundle URLForResource:@"fishtank"  withExtension:@"mp4"];
    
    // Filter out nils in case a file is missing
    NSArray<NSURL *> *availableVideos = @[fireplaceURL, fishtankURL];
    availableVideos = [availableVideos filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"self != nil"]];
    
    NSURL *videoURL = nil;
    if (availableVideos.count > 0) {
        // Randomly pick one if both are available
        NSUInteger randomIndex = arc4random_uniform((uint32_t)availableVideos.count);
        videoURL = availableVideos[randomIndex];
    }
    
    if (!videoURL) {
        // Fallback for testing if no local file is found in the bundle
        videoURL = [NSURL URLWithString:@"https://samplelib.com/mp4/sample-5s-720p.mp4"];
    }
    
    if (videoURL) {
        AVPlayerItem *item = [AVPlayerItem playerItemWithURL:videoURL];
        _player = [AVPlayer playerWithPlayerItem:item];
        _player.actionAtItemEnd = AVPlayerActionAtItemEndNone; // We will loop manually
        // _player.muted = YES; // Uncomment this line if you prefer silent playback
        
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(playerItemDidReachEnd:)
                                                     name:AVPlayerItemDidPlayToEndTimeNotification
                                                   object:item];
        
        _playerView = [[AVPlayerView alloc] initWithFrame:self.bounds];
        _playerView.player = _player;
        _playerView.controlsStyle = AVPlayerViewControlsStyleNone; // Hide controls for screensaver
        _playerView.videoGravity = AVLayerVideoGravityResizeAspectFill; // Fill screen without stretching
        [_playerView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        
        [self addSubview:_playerView];
        [_player play];
    }
}

- (void)playerItemDidReachEnd:(NSNotification *)notification {
    [_player seekToTime:kCMTimeZero];
    [_player play];
}

- (void)stopAnimation {
    if (_player) {
        [_player pause];
        [[NSNotificationCenter defaultCenter] removeObserver:self];
        _player = nil;
    }
    if (_playerView) {
        [_playerView removeFromSuperview];
        _playerView = nil;
    }
    [super stopAnimation];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)animateOneFrame {
    // AVPlayer handles its own rendering, so no manual drawing is needed here.
    return;
}

- (BOOL)hasConfigureSheet { return NO; }
- (NSWindow *)configureSheet { return nil; }

@end