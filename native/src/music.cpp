#include "music.h"
#include "music_platform.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QRandomGenerator>
#include <algorithm>

static void initializeAudio() { Q_INIT_RESOURCE(alert_audio); }
namespace {
const QJsonObject& catalog() {
    static const auto value = [] {
        initializeAudio();
        QFile file(":/podlord/audio/catalog.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
    }();
    return value;
}
}
namespace podlord {
QVariantList bundledSounds() {
    static const auto sounds = [] {
        QVariantList result{QVariantMap{{"id", "none"}, {"name", "No sound"}, {"purpose", "Silent alert action."}, {"asset", ""}}};
        for (const auto& sound : catalog()["sounds"].toArray()) result.append(sound.toObject().toVariantMap());
        return result;
    }();
    return sounds;
}
QVariantList bundledTracks() { static const auto tracks = catalog()["tracks"].toArray().toVariantList(); return tracks; }
QString migratedSound(const QString& id) { return catalog()["legacySounds"].toObject().value(id).toString(id); }
Music::Music(QObject* parent) : QObject(parent), tracks_(bundledTracks()), platform_(std::make_unique<MusicPlatform>(this)) {
    for (int index = 0; index < tracks_.size(); ++index) order_.append(index);
    std::shuffle(order_.begin(), order_.end(), *QRandomGenerator::global());
    if (!order_.isEmpty()) selected_ = order_.first();
}
Music::~Music() { pause(); }
QString Music::currentId() const { return tracks_.isEmpty() ? QString{} : tracks_[selected_].toMap()["id"].toString(); }
bool Music::playing() const { return requested_; }
bool Music::play() {
    if (tracks_.isEmpty() || qEnvironmentVariableIntValue("PODLORD_DISABLE_AUDIO") != 0) return false;
    if (playing()) return true;
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        error_ = "music.noOutput";
        emit changed();
        return false;
    }
    if (!player_) {
        output_ = std::make_unique<QAudioOutput>();
        output_->setMuted(muted_);
        output_->setVolume(0.35);
        player_ = std::make_unique<QMediaPlayer>();
        player_->setAudioOutput(output_.get());
        player_->setSource(QUrl(tracks_[selected_].toMap()["asset"].toString()));
        connect(player_.get(), &QMediaPlayer::playbackStateChanged, this, &Music::changed);
        connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia && requested_) {
                next();
            }
        });
        connect(player_.get(), &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString&) {
            pause();
            error_ = "music.failed";
            emit changed();
        });
        connect(output_.get(), &QAudioOutput::deviceChanged, this, [this] { pause(); });
        auto* devices = new QMediaDevices(this);
        connect(devices, &QMediaDevices::audioOutputsChanged, this, [this] { pause(); });
    }
    if (!platform_->activate(tracks_[selected_].toMap()["name"].toString())) {
        error_ = "music.unavailable";
        emit changed();
        return false;
    }
    error_.clear();
    if (player_->mediaStatus() == QMediaPlayer::InvalidMedia)
        player_->setSource(QUrl(tracks_[selected_].toMap()["asset"].toString()));
    requested_ = true;
    player_->play();
    emit changed();
    return true;
}
bool Music::pause() {
    requested_ = false;
    if (player_) player_->pause();
    platform_->release();
    emit changed();
    return true;
}
bool Music::next() {
    if (tracks_.isEmpty()) return false;
    const int previous = selected_;
    if (++orderIndex_ >= order_.size()) {
        std::shuffle(order_.begin(), order_.end(), *QRandomGenerator::global());
        if (order_.size() > 1 && order_.first() == previous) order_.swapItemsAt(0, 1);
        orderIndex_ = 0;
    }
    selected_ = order_[orderIndex_];
    if (player_) {
        player_->stop();
        player_->setSource(QUrl(tracks_[selected_].toMap()["asset"].toString()));
        if (requested_) {
            if (!platform_->update(tracks_[selected_].toMap()["name"].toString())) {
                pause();
                error_ = "music.unavailable";
                emit changed();
                return false;
            }
            player_->play();
        }
    }
    emit changed();
    return true;
}
void Music::setMuted(bool muted) {
    if (muted_ == muted) return;
    muted_ = muted;
    if (output_) output_->setMuted(muted);
    emit changed();
}
}
