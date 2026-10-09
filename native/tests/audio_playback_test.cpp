#include <QAudioOutput>
#include <QFile>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <cstdio>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    Q_INIT_RESOURCE(alert_audio);
    if (argc != 3) return 2;
    const QUrl source(QString::fromLocal8Bit(argv[1]));
    bool valid = false;
    const int repetitions = QString::fromLocal8Bit(argv[2]).toInt(&valid);
    if (!valid || repetitions < 1 || repetitions > 2 || source.scheme() != "qrc"
        || !QFile::exists(":" + source.path())) return 2;
    QAudioOutput output;
    output.setVolume(0);
    QMediaPlayer player;
    player.setAudioOutput(&output);
    int completed = 0;
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
        [&](QMediaPlayer::Error, const QString& error) {
            std::fprintf(stderr, "Embedded sound failed: %s\n", qPrintable(error));
            app.exit(1);
        });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app,
        [&](QMediaPlayer::MediaStatus status) {
            if (status != QMediaPlayer::EndOfMedia) return;
            if (!player.hasAudio() || player.duration() <= 0 || player.error() != QMediaPlayer::NoError) {
                std::fprintf(stderr, "Playback ended without a decoded audio track\n");
                app.exit(1);
                return;
            }
            if (++completed == repetitions) {
                std::printf("Decoded %s through the native backend, %d playback(s)\n", argv[1], completed);
                app.exit(0);
                return;
            }
            QTimer::singleShot(0, &app, [&] { player.stop(); player.play(); });
        });
    QTimer::singleShot(25000, &app, [&] {
        std::fprintf(stderr, "Embedded sound playback timed out: %s\n", qPrintable(player.errorString()));
        app.exit(1);
    });
    player.setSource(source);
    player.play();
    return app.exec();
}
