#include "music_platform.h"
#include "music.h"
#import <AVFAudio/AVFAudio.h>
#import <MediaPlayer/MediaPlayer.h>

namespace podlord {
struct MusicPlatform::State {
    Music* owner;
    id interruption = nil, route = nil, pause = nil, next = nil;
    bool active = false;
};
MusicPlatform::MusicPlatform(Music* owner) : state_(std::make_unique<State>()) {
    state_->owner = owner;
    auto* center = [NSNotificationCenter defaultCenter];
    state_->interruption = [center addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:nil usingBlock:^(NSNotification* note) {
        if ([note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan)
            QMetaObject::invokeMethod(owner, [owner] { owner->pause(); }, Qt::QueuedConnection);
    }];
    state_->route = [center addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:nil usingBlock:^(NSNotification* note) {
        if ([note.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue] == AVAudioSessionRouteChangeReasonOldDeviceUnavailable)
            QMetaObject::invokeMethod(owner, [owner] { owner->pause(); }, Qt::QueuedConnection);
    }];
}
MusicPlatform::~MusicPlatform() {
    release();
    [[NSNotificationCenter defaultCenter] removeObserver:state_->interruption];
    [[NSNotificationCenter defaultCenter] removeObserver:state_->route];
}
bool MusicPlatform::activate(const QString& title) {
    NSError* error = nil;
    auto* session = [AVAudioSession sharedInstance];
    if (![session setCategory:AVAudioSessionCategoryPlayback error:&error] || ![session setActive:YES error:&error]) return false;
    state_->active = true;
    auto* owner = state_->owner;
    auto* commands = [MPRemoteCommandCenter sharedCommandCenter];
    if (!state_->pause) state_->pause = [commands.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        QMetaObject::invokeMethod(owner, [owner] { owner->pause(); }, Qt::QueuedConnection);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    if (!state_->next) state_->next = [commands.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        QMetaObject::invokeMethod(owner, [owner] { owner->next(); }, Qt::QueuedConnection);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    update(title);
    return true;
}
bool MusicPlatform::update(const QString& title) {
    if (!state_->active) return false;
    NSString* name = [NSString stringWithUTF8String:title.toUtf8().constData()];
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = @{MPMediaItemPropertyTitle: name, MPMediaItemPropertyArtist: @"Podlord", MPNowPlayingInfoPropertyPlaybackRate: @1};
    return true;
}
void MusicPlatform::release() {
    if (!state_->active) return;
    state_->active = false;
    auto* commands = [MPRemoteCommandCenter sharedCommandCenter];
    if (state_->pause) [commands.pauseCommand removeTarget:state_->pause];
    if (state_->next) [commands.nextTrackCommand removeTarget:state_->next];
    state_->pause = nil; state_->next = nil;
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    [[AVAudioSession sharedInstance] setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:nil];
}
}
