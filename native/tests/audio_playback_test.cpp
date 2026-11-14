#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSink>
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <memory>

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(alert_audio);
    const QByteArray mode = argc > 1 ? argv[1] : "";
    const bool decode = mode == "decode";
    const bool repeat = mode == "repeat";
    const bool changeSource = mode == "switch";
    if ((!decode && !repeat && !changeSource) || argc != (changeSource ? 4 : 3)) {
        std::fprintf(stderr, "Usage: audio-playback-test decode|repeat QRC_URL | switch QRC_URL QRC_URL\n");
        return 2;
    }
    const QUrl first(QString::fromLocal8Bit(argv[2]));
    const QUrl second(changeSource ? QString::fromLocal8Bit(argv[3]) : first.toString());
    const auto valid = [](const QUrl& source) {
        return source.scheme() == "qrc" && source.host().isEmpty()
            && !source.hasQuery() && !source.hasFragment() && QFile::exists(":" + source.path());
    };
    if (!valid(first) || !valid(second) || (changeSource && first == second)) return 2;
    std::unique_ptr<QCoreApplication> app;
    if (decode) app = std::make_unique<QCoreApplication>(argc, argv);
    else app = std::make_unique<QGuiApplication>(argc, argv);

    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(25000);
    bool finished = false;
    const auto finish = [&](int result) {
        if (finished) return;
        finished = true;
        watchdog.stop();
        app->exit(result);
    };
    if (decode) {
        // QFile keeps embedded media seekable without FFmpeg's unsupported qrc protocol.
        QFile file(":" + first.path());
        if (!file.open(QIODevice::ReadOnly)) return 2;
        QAudioDecoder decoder;
        qint64 frames = 0;
        qint64 bytes = 0;
        QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), app.get(), [&](QAudioDecoder::Error error) {
            if (error == QAudioDecoder::NoError) return;
            std::fprintf(stderr, "Decode failed for %s: %s\n", qPrintable(first.toString()), qPrintable(decoder.errorString()));
            finish(1);
        });
        QObject::connect(&decoder, &QAudioDecoder::bufferReady, app.get(), [&] {
            if (finished) return;
            const auto buffer = decoder.read();
            if (!buffer.isValid() || buffer.frameCount() <= 0 || buffer.byteCount() <= 0) {
                std::fprintf(stderr, "Invalid decoded PCM for %s\n", qPrintable(first.toString()));
                finish(1);
                return;
            }
            frames += buffer.frameCount();
            bytes += buffer.byteCount();
        });
        QObject::connect(&decoder, &QAudioDecoder::finished, app.get(), [&] {
            if (finished) return;
            if (frames <= 0 || bytes <= 0 || decoder.error() != QAudioDecoder::NoError || decoder.bufferAvailable()) {
                std::fprintf(stderr, "Incomplete decode for %s: frames=%lld bytes=%lld pending=%d\n", qPrintable(first.toString()), static_cast<long long>(frames), static_cast<long long>(bytes), decoder.bufferAvailable());
                finish(1);
                return;
            }
            std::printf("Decoded %s completely without an audio output: %lld PCM frames, %lld bytes\n", qPrintable(first.toString()), static_cast<long long>(frames), static_cast<long long>(bytes));
            finish(0);
        });
        QObject::connect(&watchdog, &QTimer::timeout, app.get(), [&] {
            std::fprintf(stderr, "Decode timed out for %s: frames=%lld bytes=%lld\n", qPrintable(first.toString()), static_cast<long long>(frames), static_cast<long long>(bytes));
            finish(1);
        });
        QTimer::singleShot(0, app.get(), [&] {
            watchdog.start();
            decoder.setSourceDevice(&file);
            decoder.start();
        });
        return app->exec();
    }

    QAudioDevice selected;
    QMediaDevices devices;
    QBuffer probeData;
    std::unique_ptr<QAudioSink> probe;
    std::unique_ptr<QAudioOutput> output;
    std::unique_ptr<QMediaPlayer> player;
    QTimer probePoll;
    probePoll.setInterval(5);
    bool probeComplete = false;
    int completed = 0;
    const auto available = [&] {
        if (selected.isNull()) return false;
        for (const auto& device : QMediaDevices::audioOutputs())
            if (device.id() == selected.id()) return true;
        return false;
    };
    QObject::connect(&devices, &QMediaDevices::audioOutputsChanged, app.get(), [&] {
        if (!finished && !selected.isNull() && !available()) {
            std::fprintf(stderr, "Selected audio output became unavailable\n");
            finish(1);
        }
    });
    const auto startPlayback = [&] {
        output = std::make_unique<QAudioOutput>();
        output->setDevice(selected);
        output->setMuted(true);
        if (!available() || output->device().isNull() || output->device().id() != selected.id()) {
            std::fprintf(stderr, "Probed audio output is no longer selected\n");
            finish(1);
            return;
        }
        QObject::connect(output.get(), &QAudioOutput::deviceChanged, app.get(), [&] {
            if (finished) return;
            std::fprintf(stderr, "Playback audio output changed after the separate sink probe\n");
            finish(1);
        });
        player = std::make_unique<QMediaPlayer>();
        player->setAudioOutput(output.get());
        QObject::connect(player.get(), &QMediaPlayer::errorOccurred, app.get(), [&](QMediaPlayer::Error, const QString& message) {
            std::fprintf(stderr, "Playback failed: %s\n", qPrintable(message));
            finish(1);
        });
        QObject::connect(player.get(), &QMediaPlayer::mediaStatusChanged, app.get(), [&](QMediaPlayer::MediaStatus status) {
            if (finished || status != QMediaPlayer::EndOfMedia) return;
            const auto& expected = completed == 0 ? first : second;
            if (!available() || output->device().id() != selected.id() || !output->isMuted()
                || player->source() != expected || !player->hasAudio() || player->duration() <= 0
                || player->error() != QMediaPlayer::NoError) {
                std::fprintf(stderr, "Incomplete muted playback for %s\n", qPrintable(player->source().toString()));
                finish(1);
                return;
            }
            if (++completed == 2) {
                std::printf("Completed muted QMediaPlayer %s: %s then %s; device evidence is from a separate sink probe, not the player's private sink\n", mode.constData(), qPrintable(first.toString()), qPrintable(second.toString()));
                finish(0);
                return;
            }
            QTimer::singleShot(0, app.get(), [&] {
                if (finished) return;
                player->stop();
                if (changeSource) player->setSource(second);
                player->play();
            });
        });
        player->setSource(first);
        player->play();
    };
    const auto checkProbe = [&] {
        if (finished || probeComplete) return;
        if (!available() || probe->isNull() || probe->error() != QtAudio::NoError
            || (probe->state() != QtAudio::ActiveState && probe->state() != QtAudio::IdleState)) {
            std::fprintf(stderr, "Separate silent sink probe failed: state=%d error=%d processed=%lld\n",
                int(probe->state()), int(probe->error()), static_cast<long long>(probe->processedUSecs()));
            finish(1);
            return;
        }
        if (probe->processedUSecs() < 100000) return;
        const auto processed = probe->processedUSecs();
        probePoll.stop();
        probeComplete = true;
        probe->reset();
        probe.reset();
        probeData.close();
        std::printf("Separate zero-volume QAudioSink probe processed %lld us; this does not observe QMediaPlayer's private sink\n", static_cast<long long>(processed));
        startPlayback();
    };
    QObject::connect(&probePoll, &QTimer::timeout, app.get(), checkProbe);
    QObject::connect(&watchdog, &QTimer::timeout, app.get(), [&] {
        if (player) {
            std::fprintf(stderr, "Playback timed out: state=%d status=%d completed=%d position=%lld duration=%lld audio=%d muted=%d device=%s\n", int(player->playbackState()), int(player->mediaStatus()), completed, static_cast<long long>(player->position()), static_cast<long long>(player->duration()), player->hasAudio(), output->isMuted(), qPrintable(output->device().description()));
        } else {
            std::fprintf(stderr, "Separate silent sink probe timed out: state=%d error=%d processed=%lld\n",
                probe ? int(probe->state()) : -1, probe ? int(probe->error()) : -1,
                probe ? static_cast<long long>(probe->processedUSecs()) : 0LL);
        }
        finish(1);
    });
    QTimer::singleShot(0, app.get(), [&] {
        watchdog.start();
        const auto outputs = QMediaDevices::audioOutputs();
        const auto requested = qgetenv("PODLORD_AUDIO_TEST_OUTPUT_ID");
        if (requested.isEmpty()) selected = QMediaDevices::defaultAudioOutput();
        else for (const auto& device : outputs)
            if (device.id() == requested) { selected = device; break; }
        if (outputs.isEmpty() || !available()) {
            std::fprintf(stderr, "Selected audio output is unavailable%s\n", requested.isEmpty() ? "" : " (PODLORD_AUDIO_TEST_OUTPUT_ID)");
            finish(1);
            return;
        }
        const auto format = selected.preferredFormat();
        const auto bytes = format.bytesForDuration(1000000);
        if (!format.isValid() || !selected.isFormatSupported(format) || bytes <= 0) {
            std::fprintf(stderr, "Selected audio output has no supported preferred PCM format\n");
            finish(1);
            return;
        }
        // Keep more silence queued than the brief progress check needs, avoiding an artificial underrun.
        probeData.setData(QByteArray(bytes, format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : char(0)));
        if (!probeData.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "Could not open silent PCM for the separate sink probe\n");
            finish(1);
            return;
        }
        probe = std::make_unique<QAudioSink>(selected, format);
        probe->setVolume(0);
        QObject::connect(probe.get(), &QAudioSink::stateChanged, app.get(), [&](QtAudio::State state) {
            if (finished || probeComplete) return;
            if (probe->error() != QtAudio::NoError || state == QtAudio::StoppedState || state == QtAudio::SuspendedState) {
                std::fprintf(stderr, "Separate silent sink probe failed: state=%d error=%d\n", int(state), int(probe->error()));
                finish(1);
            }
        });
        probe->start(&probeData);
        checkProbe();
        if (!finished && !probeComplete) probePoll.start();
    });
    const int result = app->exec();
    probePoll.stop();
    probeComplete = true;
    if (probe) { probe->reset(); probe.reset(); }
    probeData.close();
    return result;
}
