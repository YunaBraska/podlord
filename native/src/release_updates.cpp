#include "release_updates.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QVersionNumber>
#include <algorithm>
#include <cmath>

namespace podlord {
namespace {
constexpr qint64 weekMs = 7LL * 24 * 60 * 60 * 1000;
constexpr qsizetype responseLimit = 1024 * 1024;
QVersionNumber version(const QString& text) {
    static const QRegularExpression syntax("^v?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
    if (text.size() > 64 || !syntax.match(text).hasMatch()) return {};
    const auto digits = text.startsWith('v') ? text.mid(1) : text;
    qsizetype suffix = 0;
    const auto parsed = QVersionNumber::fromString(digits, &suffix);
    return suffix == digits.size() && parsed.segmentCount() == 3 ? parsed : QVersionNumber{};
}
QString releaseUrl(const QString& tag) {
    return version(tag).isNull() ? QString{} : "https://github.com/YunaBraska/podlord/releases/tag/" + tag;
}
QString downloadUrl(const QString& tag) {
    const auto name = ReleaseUpdates::compatibleAssetName();
    return name.isEmpty() || version(tag).isNull() ? QString{} : "https://github.com/YunaBraska/podlord/releases/download/" + tag + '/' + name;
}
bool newer(const QString& current, const QString& latest) {
    const auto a = version(current), b = version(latest);
    return !a.isNull() && !b.isNull() && QVersionNumber::compare(b, a) > 0;
}
bool validEndpoint(const QUrl& url) {
    return url == ReleaseUpdates::officialEndpoint()
        || (url.isValid() && url.scheme() == "http" && url.host() == "127.0.0.1"
            && url.port() > 0 && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment());
}
} // namespace
QUrl ReleaseUpdates::officialEndpoint() { return QUrl("https://api.github.com/repos/YunaBraska/podlord/releases/latest"); }
QString ReleaseUpdates::compatibleAssetName() {
    const auto cpu = QSysInfo::buildCpuArchitecture();
    const auto arch = cpu == "arm64" ? QString("arm64") : cpu == "x86_64" ? QString("x64") : QString{};
    if (arch.isEmpty()) return {};
#if defined(Q_OS_MACOS)
    return "podlord-native-macos-" + arch + ".zip";
#elif defined(Q_OS_WIN)
    return "podlord-native-win-" + arch + ".zip";
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return "podlord-native-linux-" + arch + ".tar.gz";
#else
    return {};
#endif
}
ReleaseUpdates::ReleaseUpdates(QString profile, QObject* parent, std::function<QDateTime()> now, QUrl endpoint)
    : QObject(parent), profile_(std::move(profile)), currentVersion_(QCoreApplication::applicationVersion()),
      now_(std::move(now)), endpoint_(std::move(endpoint)) {
    const auto disabled = qEnvironmentVariable("PODLORD_DISABLE_UPDATE_CHECK");
    enabled_ = disabled != "1" && disabled.compare("true", Qt::CaseInsensitive) != 0;
    weekly_.setSingleShot(true); deadline_.setSingleShot(true);
    connect(&weekly_, &QTimer::timeout, this, &ReleaseUpdates::checkIfDue);
    connect(&deadline_, &QTimer::timeout, this, [this] { complete("The release server did not finish within 10 seconds. Retry explicitly."); });
    const auto cached = load();
    if (const auto* failure = std::get_if<Failure>(&cached)) localError_ = failure->message;
    else check_ = std::get<Check>(cached);
}
ReleaseUpdates::~ReleaseUpdates() {
    if (reply_) { disconnect(reply_, nullptr, this, nullptr); reply_->abort(); }
}
QVariantMap ReleaseUpdates::state() const {
    const bool isNewer = newer(currentVersion_, check_.latestVersion);
    const bool available = isNewer && !check_.downloadUrl.isEmpty();
    const auto error = localError_.isEmpty() ? check_.error : localError_;
    QString status;
    if (reply_) status = "Checking for updates...";
    else if (!error.isEmpty()) status = error;
    else if (!enabled_) status = "Update checks are disabled for this run.";
    else if (available) status = QString("Native update %1 is available; installed version %2.").arg(check_.latestVersion, currentVersion_);
    else if (isNewer) status = "A newer release exists, but no compatible native package is published.";
    else if (!check_.latestVersion.isEmpty()) status = "The installed native version is up to date.";
    else if (check_.checkedAt.isValid()) status = "No published release was found.";
    else status = "Updates are checked automatically once a week.";
    return {{"busy", !reply_.isNull()}, {"enabled", enabled_}, {"available", available}, {"newer", isNewer},
        {"currentVersion", currentVersion_}, {"latestVersion", check_.latestVersion},
        {"lastCheckedAt", check_.checkedAt.toString(Qt::ISODateWithMs)}, {"releaseUrl", check_.releaseUrl},
        {"downloadUrl", check_.downloadUrl}, {"error", error}, {"status", status}};
}
Result<ReleaseUpdates::Check> ReleaseUpdates::load() const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const auto path = QDir(profile_).filePath("release-check.json");
    const auto invalid = Failure{StoreError::InvalidData, "Invalid release-check cache; existing data was retained. Repair it before checking again."};
    if (QFileInfo(path).isSymLink()) return invalid;
    if (!QFileInfo::exists(path)) return Check{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return Failure{StoreError::ReadFailed, "Cannot read the private release-check cache."};
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read the private release-check cache."};
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    const auto root = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject() || root.size() != 7 || root["version"] != 1) return invalid;
    for (const auto* key : {"checkedAt", "currentVersion", "latestVersion", "releaseUrl", "downloadUrl", "error"})
        if (!root[QLatin1String(key)].isString()) return invalid;
    Check value{QDateTime::fromString(root["checkedAt"].toString(), Qt::ISODateWithMs), root["currentVersion"].toString(),
        root["latestVersion"].toString(), root["releaseUrl"].toString(), root["downloadUrl"].toString(), root["error"].toString()};
    if (!value.checkedAt.isValid() || version(value.currentVersion).isNull() || value.error.size() > 256
        || std::any_of(value.error.cbegin(), value.error.cend(), [](QChar ch) { return ch.category() == QChar::Other_Control; })) return invalid;
    if (value.latestVersion.isEmpty()) {
        if (!value.releaseUrl.isEmpty() || !value.downloadUrl.isEmpty()) return invalid;
    } else if (version(value.latestVersion).isNull() || value.releaseUrl != releaseUrl(value.latestVersion)
        || (!value.downloadUrl.isEmpty() && value.downloadUrl != downloadUrl(value.latestVersion))) return invalid;
    value.checkedAt = value.checkedAt.toUTC();
    return value;
}
Result<ReleaseUpdates::Check> ReleaseUpdates::save(const Check& value) const {
    const QJsonObject root{{"version", 1}, {"checkedAt", value.checkedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"currentVersion", value.currentVersion}, {"latestVersion", value.latestVersion},
        {"releaseUrl", value.releaseUrl}, {"downloadUrl", value.downloadUrl}, {"error", value.error}};
    QSaveFile file(QDir(profile_).filePath("release-check.json")); file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot atomically save the private release-check cache. No automatic retry was scheduled."};
    return value;
}
bool ReleaseUpdates::due(const QDateTime& now) const {
    return !check_.checkedAt.isValid() || check_.currentVersion != currentVersion_ || check_.checkedAt > now
        || check_.checkedAt.msecsTo(now) >= weekMs;
}
bool ReleaseUpdates::schedule() {
    if (!automatic_ || !enabled_ || !localError_.isEmpty() || !check_.checkedAt.isValid()) return false;
    const auto now = now_();
    if (!now.isValid()) return false;
    weekly_.start(static_cast<int>(std::clamp(check_.checkedAt.addMSecs(weekMs).toMSecsSinceEpoch() - now.toMSecsSinceEpoch(), 1LL, weekMs)));
    return true;
}
bool ReleaseUpdates::fail(const QString& error) {
    localError_ = error; if (!reply_) lock_.reset(); emit changed(); return false;
}
bool ReleaseUpdates::startAutomaticChecks() {
    if (automatic_) return false;
    automatic_ = true;
    return checkIfDue();
}
bool ReleaseUpdates::checkIfDue() { return check(false); }
bool ReleaseUpdates::checkNow() { return check(true); }
bool ReleaseUpdates::check(bool force) {
    if (!enabled_ || reply_) return false;
    if (!now_) return fail("The application clock is unavailable; no release request was sent.");
    const auto now = now_();
    if (!now.isValid() || version(currentVersion_).isNull()) return fail("The application version or clock is invalid; no release request was sent.");
    if (!validEndpoint(endpoint_)) return fail("The release endpoint is not the official server or an explicit loopback test server.");
    if (!force && !due(now)) { schedule(); return false; }
    weekly_.stop();
    if (const auto failure = profileFailure(profile_)) return fail(failure->message);
    const bool created = !QFileInfo::exists(profile_);
    if (!QDir().mkpath(profile_) || (created && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return fail("Cannot create the private release-check profile.");
    const auto path = QDir(profile_).filePath("release-check.json");
    if (QFileInfo(path + ".lock").isSymLink() || QFileInfo(path + ".lock").isDir()) return fail("The release-check lock path is invalid; no request was sent.");
    lock_ = std::make_unique<QLockFile>(path + ".lock"); lock_->setStaleLockTime(0);
    if (!lock_->tryLock(0)) return fail("Another process is checking releases, or the private cache cannot be locked. Retry explicitly.");
    const auto stored = load();
    if (const auto* failure = std::get_if<Failure>(&stored)) return fail(failure->message);
    check_ = std::get<Check>(stored); localError_.clear();
    if (!force && !due(now)) { lock_.reset(); schedule(); emit changed(); return false; }
    check_.checkedAt = now.toUTC(); check_.currentVersion = currentVersion_;
    const auto stamped = save(check_);
    if (const auto* failure = std::get_if<Failure>(&stamped)) return fail(failure->message);
    response_.clear();
    QNetworkRequest request(endpoint_);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2026-03-10");
    request.setRawHeader("User-Agent", "Podlord-Native/" + currentVersion_.toLatin1());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    reply_ = network_.get(request); reply_->setReadBufferSize(responseLimit + 1);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (!reply_) return;
        response_ += reply_->readAll();
        if (response_.size() > responseLimit) complete("The release response exceeds the 1 MiB metadata limit. Retry explicitly.");
    });
    connect(reply_, &QNetworkReply::finished, this, [this] { complete(); });
    deadline_.start(10000); emit changed(); return true;
}
bool ReleaseUpdates::complete(const QString& failure) {
    if (!reply_) return false;
    deadline_.stop();
    auto* reply = reply_.data(); reply_.clear(); disconnect(reply, nullptr, this, nullptr);
    response_ += reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString error = failure;
    if (error.isEmpty() && response_.size() > responseLimit) error = "The release response exceeds the 1 MiB metadata limit. Retry explicitly.";
    if (error.isEmpty() && status != 200 && status != 404)
        error = status ? QString("Release check failed (HTTP %1). Retry explicitly.").arg(status) : QString("The release server could not be reached. Retry explicitly.");
    if (error.isEmpty() && reply->error() != QNetworkReply::NoError && status != 404) error = "The release response did not finish successfully. Retry explicitly.";
    if (reply->isRunning()) reply->abort();
    reply->deleteLater();
    auto next = check_;
    if (error.isEmpty() && status == 404) { next.latestVersion.clear(); next.releaseUrl.clear(); next.downloadUrl.clear(); }
    else if (error.isEmpty()) {
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(response_, &parse);
        const auto root = document.object();
        const auto tag = root["tag_name"].toString();
        if (parse.error != QJsonParseError::NoError || !document.isObject() || version(tag).isNull()
            || !root["draft"].isBool() || root["draft"].toBool() || !root["prerelease"].isBool() || root["prerelease"].toBool()
            || root["html_url"].toString() != releaseUrl(tag) || !root["assets"].isArray())
            error = "The release server returned invalid stable-release metadata. Retry explicitly.";
        else {
            next.latestVersion = tag; next.releaseUrl = releaseUrl(tag); next.downloadUrl.clear();
            bool found = false;
            for (const auto& value : root["assets"].toArray()) {
                const auto asset = value.toObject();
                if (compatibleAssetName().isEmpty() || asset["name"].toString() != compatibleAssetName()) continue;
                const double size = asset["size"].toDouble(-1);
                if (found || asset["state"] != "uploaded" || size <= 0 || size > 50000000 || std::floor(size) != size
                    || asset["browser_download_url"].toString() != downloadUrl(tag)) {
                    error = "The compatible native release asset is invalid or exceeds the 50 MB package limit. Retry explicitly."; break;
                }
                found = true; next.downloadUrl = downloadUrl(tag);
            }
        }
    }
    response_.clear(); response_.squeeze();
    if (error.isEmpty()) check_ = std::move(next);
    check_.error = error;
    const auto saved = save(check_);
    lock_.reset();
    localError_ = std::holds_alternative<Failure>(saved) ? std::get<Failure>(saved).message : QString{};
    schedule(); emit changed(); return error.isEmpty() && localError_.isEmpty();
}
bool ReleaseUpdates::openDownload() {
    if (!state().value("available").toBool()) return fail("No compatible newer native package is available.");
    if (!QDesktopServices::openUrl(QUrl(check_.downloadUrl))) return fail("The browser could not open the native download. Retry explicitly.");
    localError_.clear(); emit changed(); return true;
}
bool ReleaseUpdates::openRelease() {
    if (check_.releaseUrl.isEmpty()) return fail("No verified release page is available.");
    if (!QDesktopServices::openUrl(QUrl(check_.releaseUrl))) return fail("The browser could not open the release page. Retry explicitly.");
    localError_.clear(); emit changed(); return true;
}
} // namespace podlord
