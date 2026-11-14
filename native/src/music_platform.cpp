#include "music_platform.h"
#include "music.h"
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QCoreApplication>
#endif

namespace podlord {
struct MusicPlatform::State { Music* owner; bool active = false; };
MusicPlatform::MusicPlatform(Music* owner) : state_(std::make_unique<State>(State{owner})) {}
MusicPlatform::~MusicPlatform() { release(); }
#ifdef Q_OS_ANDROID
namespace {
void command(JNIEnv*, jclass, jlong handle, jint action) {
    auto* owner = reinterpret_cast<Music*>(handle);
    QMetaObject::invokeMethod(owner, [owner, action] {
        if (action == 1) owner->next();
        else owner->pause();
    }, Qt::QueuedConnection);
}
bool registered() {
    static const bool ready = [] {
        QJniEnvironment env;
        const JNINativeMethod methods[]{{"nativeCommand", "(JI)V", reinterpret_cast<void*>(command)}};
        return env.registerNativeMethods("berlin/yuna/podlord/MusicService", methods, 1);
    }();
    return ready;
}
}
bool MusicPlatform::activate(const QString& title) {
    if (!registered()) return false;
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto text = QJniObject::fromString(title);
    const auto handle = reinterpret_cast<jlong>(state_->owner);
    // Service/media Binder calls are thread-safe. Qt's Activity dispatch queue waits while paused.
    state_->active = QJniObject::callStaticMethod<jboolean>("berlin/yuna/podlord/MusicService", "start",
        "(Landroid/content/Context;JLjava/lang/String;)Z", context.object(), handle, text.object<jstring>());
    return state_->active;
}
bool MusicPlatform::update(const QString& title) { return activate(title); }
void MusicPlatform::release() {
    if (!state_->active) return;
    state_->active = false;
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto handle = reinterpret_cast<jlong>(state_->owner);
    QJniObject::callStaticMethod<void>("berlin/yuna/podlord/MusicService", "stop",
        "(Landroid/content/Context;J)V", context.object(), handle);
}
#else
bool MusicPlatform::activate(const QString&) { return true; }
bool MusicPlatform::update(const QString&) { return true; }
void MusicPlatform::release() {}
#endif
}
