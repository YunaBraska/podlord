#include "platform_tls.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <chrono>
#include <optional>
#include <thread>

#ifdef Q_OS_MACOS
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) return 2;
    const auto document = QJsonDocument::fromJson(input.readAll()).object();
    if (!document["expected"].isBool() || !document["errors"].isArray()) return 2;
    const auto certificates = [](const QString& path) {
        QList<QSslCertificate> result;
        if (path.isEmpty()) return result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QList<QSslCertificate>{QSslCertificate{}};
        const auto bytes = file.readAll();
        result = QSslCertificate::fromData(bytes);
        if (result.isEmpty()) result.append(QSslCertificate{});
        return result;
    };
    QList<QSslError> errors;
    for (const auto& error : document["errors"].toArray()) errors.append(QSslError(static_cast<QSslError::SslError>(error.toInt())));
    const auto chain = certificates(document["peer"].toString());
    const auto authorities = certificates(document["authorities"].toString());
    QElapsedTimer elapsed; elapsed.start();
    std::stop_source stop;
    if (document["canceled"].toBool()) stop.request_stop();
    const auto cancelAfter = document["cancelAfterMs"].toInt();
    std::optional<std::jthread> cancellation;
    if (cancelAfter > 0) cancellation.emplace([&] { std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfter)); stop.request_stop(); });
#ifdef Q_OS_MACOS
    if (const auto arm = reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT, "podlord_test_arm_platform_fault"))) arm();
#endif
    const bool actual = podlord::platformTrustsPeer(chain, document["name"].toString(), authorities, errors, stop.get_token());
    const auto completedMs = elapsed.elapsed();
    std::printf("Native peer trust: expected=%d actual=%d elapsed_us=%lld\n", document["expected"].toBool(), actual,
        static_cast<long long>(elapsed.nsecsElapsed() / 1000));
    if (cancelAfter > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1700));
        if (completedMs < cancelAfter || completedMs >= 1500) return 1;
    }
    return actual == document["expected"].toBool() ? 0 : 1;
}
