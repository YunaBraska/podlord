#pragma once
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVariantList>
#include <memory>

namespace podlord {
/** Embedded, immutable audio metadata. No Kubernetes data or remote media URLs. */
QVariantList bundledSounds();
QVariantList bundledTracks();
QString migratedSound(const QString& id);
class MusicPlatform;
/** One application-owned player. Playback intent is never persisted or restored. */
class Music final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit Music(QObject* parent = nullptr);
    ~Music() override;
    QString currentId() const;
    bool playing() const;
    bool muted() const { return muted_; }
    QString error() const { return error_; }
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool next();
    void setMuted(bool muted);
signals:
    void changed();
private:
    const QVariantList tracks_;
    int selected_ = 0;
    QList<int> order_;
    int orderIndex_ = 0;
    bool muted_ = false;
    bool requested_ = false;
    QString error_;
    std::unique_ptr<MusicPlatform> platform_;
    std::unique_ptr<QAudioOutput> output_;
    std::unique_ptr<QMediaPlayer> player_;
};
}
