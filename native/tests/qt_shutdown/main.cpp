#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cstdio>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QTemporaryDir files;
    if (!files.isValid()) return 2;
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::green);
    const auto source = files.filePath("pixel.png");
    if (!image.save(source)) return 2;
    // Application ownership deliberately exercises the upstream teardown bug.
    auto* engine = new QQmlEngine(&application);
    QQmlComponent component(engine);
    component.setData("import QtQuick\nImage { asynchronous: true; cache: false; source: \""
        + QUrl::fromLocalFile(source).toEncoded() + "\" }", QUrl());
    auto* item = component.create();
    if (!item) {
        std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
        return 2;
    }
    item->setParent(engine);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &application, [&] {
        if (item->property("status").toInt() == 1) {
            std::puts("Asynchronous image ready; application-owned engine shutdown follows.");
            std::fflush(stdout);
            application.exit(0);
        } else if (item->property("status").toInt() == 3) {
            application.exit(2);
        }
    });
    poll.start(5);
    QTimer::singleShot(30000, &application, [&] { application.exit(3); });
    return application.exec();
}
