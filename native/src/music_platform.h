#pragma once
#include <QString>
#include <memory>

namespace podlord {
class Music;
/** Only OS media focus, background lifetime and remote controls live here. */
class MusicPlatform final {
public:
    explicit MusicPlatform(Music* owner);
    ~MusicPlatform();
    bool activate(const QString& title);
    bool update(const QString& title);
    void release();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
