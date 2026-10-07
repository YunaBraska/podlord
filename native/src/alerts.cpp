#include "alerts.h"
#include <QClipboard>
#include <QGuiApplication>
#include "resource_metrics.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>

namespace podlord {
namespace {
const QStringList scopes{"search", "kind", "namespace", "name", "status", "node", "image", "owner", "ready", "restarts", "age", "issue", "eventReason", "eventMessage", "problems", "activity", "recentlyChanged", "newInView", "cpu", "memory", "storage"};
const QStringList modes{"no-match", "duration", "new-in-view"};
QJsonObject encoded(const AlertCatalog& catalog) {
    QJsonArray rules; for (const auto& rule : catalog.rules) rules.append(rule.value);
    return {{"version", 1}, {"rules", rules}, {"muted", catalog.muted}, {"reducedMotion", catalog.reducedMotion}};
}
bool whole(const QJsonValue& value, int low, int high) {
    return value.isDouble() && value.toDouble() >= low && value.toDouble() <= high && value.toDouble() == value.toInt(-1);
}
std::optional<double> quantity(QString text, const QString& field) {
    if (field=="cpu" || field=="memory" || field=="storage") return metricQuantity(text, field=="cpu");
    static const QRegularExpression number("^([0-9]+(?:\\.[0-9]+)?)([a-zA-Z]*)$");
    const auto match = number.match(text); if (!match.hasMatch()) return {};
    const double value = match.captured(1).toDouble(); const auto unit = match.captured(2);
    double scale = 1;
    if (field == "age") {
        const QMap<QString, double> units{{"", 1}, {"s", 1}, {"m", 60}, {"h", 3600}, {"d", 86400}, {"w", 604800}};
        if (!units.contains(unit)) return {}; scale = units[unit];
    } else if (!unit.isEmpty()) return {};
    return std::isfinite(value * scale) ? std::optional<double>(value * scale) : std::nullopt;
}
double percentile(const QList<double>& sorted, double p) {
    if (sorted.isEmpty()) return std::numeric_limits<double>::infinity();
    const double position = (sorted.size()-1)*p; const auto lower = static_cast<qsizetype>(std::floor(position)), upper = static_cast<qsizetype>(std::ceil(position));
    return sorted[lower] + (sorted[upper]-sorted[lower])*(position-lower);
}
std::optional<double> number(const QJsonObject& row, const QString& field, const QDateTime& now) {
    if (field == "age") { const auto at = QDateTime::fromString(row["createdAt"].toString(), Qt::ISODateWithMs); return at.isValid() ? std::optional<double>(std::max<qint64>(0, at.msecsTo(now))/1000.0) : std::nullopt; }
    const auto value = row[field]; return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
}
bool numeric(const QString& field) { return QStringList{"restarts", "age", "cpu", "memory", "storage"}.contains(field); }
QString textValue(const QJsonObject& row, const QString& field) {
    if (field == "search") { QStringList values; for (auto it=row.begin(); it!=row.end(); ++it) if (it.value().isString()) values.append(it.value().toString()); return values.join(' '); }
    if (field == "namespace" && row[field].toString().isEmpty()) return "cluster";
    return row[field].toString();
}
bool criterionMatches(const AlertCriterion& criterion, const QJsonObject& row, const QMap<QString, QList<double>>& samples, const QDateTime& now, bool& valid) {
    const auto field = criterion.field, expression = criterion.expression;
    if (QStringList{"problems", "activity", "recentlyChanged", "newInView"}.contains(field)) return row[field].toBool() == (expression == "true");
    if (numeric(field)) {
        const auto value = number(row, field, now); if (!value) return false;
        const auto& sorted = samples[field];
        if (expression == "p95") return *value >= percentile(sorted, .95);
        if (expression == "outlier") {
            const double q1=percentile(sorted, .25), q3=percentile(sorted, .75);
            return *value > std::max(field == "restarts" ? 3.0 : 0.0, q3 + 1.5*(q3-q1));
        }
        bool exact = false, hasExact = false;
        for (const auto& comparison : criterion.comparisons) {
            const auto& operation = comparison.first;
            const auto expected = comparison.second;
            if (operation == ">" && !(*value > expected)) return false;
            if (operation == "<" && !(*value < expected)) return false;
            if (operation == ">=" && !(*value >= expected)) return false;
            if (operation == "<=" && !(*value <= expected)) return false;
            if (operation.isEmpty() || operation == "=") { hasExact = true; exact = exact || *value == expected; }
        }
        return !hasExact || exact;
    }
    const auto value = textValue(row, field);
    for (int i=0; i<criterion.tokens.size(); ++i) {
        const auto token = criterion.tokens[i];
        if (token.startsWith('/')) { const auto matched=criterion.patterns[i].match(value); if (!matched.isValid()) { valid=false; return false; } if (matched.hasMatch()) return true; }
        if (token.startsWith('"') && value.compare(token.mid(1, token.size()-2), Qt::CaseInsensitive) == 0) return true;
        if (token.startsWith('~') && value.startsWith(token.mid(1), Qt::CaseInsensitive)) return true;
        if (token.endsWith('~') && value.endsWith(token.chopped(1), Qt::CaseInsensitive)) return true;
        if (!token.startsWith('/') && !token.startsWith('"') && !token.startsWith('~') && !token.endsWith('~') && value.contains(token, Qt::CaseInsensitive)) return true;
    }
    return false;
}
struct Evaluated final { QMap<QString, QList<QJsonObject>> matches; QMap<QString, QJsonObject> rows; QDateTime next; QStringList errors; };
Evaluated evaluate(const QJsonArray& snapshot, const AlertCatalog& catalog, const QMap<QString,QDateTime>& changes, QDateTime now) {
    Evaluated result; QMap<QString, QList<double>> samples;
    const auto deadline = [&](QDateTime at) { if (at > now && (!result.next.isValid() || at < result.next)) result.next = at; };
    for (const auto& value : snapshot) {
        auto row = value.toObject(); const auto path = row["path"].toString();
        const auto changed = changes.value(path); const auto created=QDateTime::fromString(row["createdAt"].toString(), Qt::ISODateWithMs);
        row["recentlyChanged"] = changed.isValid() && changed.msecsTo(now) < 30000;
        row["newInView"] = row["recentlyChanged"];
        // Activity is classified once by the session cache, shared with resource filtering.
        deadline(changed.addSecs(30)); deadline(changed.addSecs(900)); deadline(created.addSecs(900));
        result.rows.insert(path, row);
        for (const auto& field : {"restarts", "age", "cpu", "memory", "storage"}) if (const auto sample=number(row, field, now)) samples[field].append(*sample);
    }
    for (auto& values : samples) std::sort(values.begin(), values.end());
    for (const auto& rule : catalog.rules) {
        if (!rule.value["enabled"].toBool()) continue;
        auto& matches = result.matches[rule.value["id"].toString()];
        bool valid=true;
        for (const auto& row : result.rows) {
            const bool matched = std::any_of(rule.groups.begin(), rule.groups.end(), [&](const auto& group) {
                return std::all_of(group.begin(), group.end(), [&](const auto& criterion) { return criterionMatches(criterion, row, samples, now, valid); });
            });
            if (matched) matches.append(row);
            if (!valid) break;
            for (const auto& group : rule.groups) for (const auto& criterion : group) if (criterion.field == "age" && criterion.expression != "p95" && criterion.expression != "outlier")
                for (const auto& comparison : criterion.comparisons) { const auto created=QDateTime::fromString(row["createdAt"].toString(), Qt::ISODateWithMs); if (created.isValid() && comparison.second < static_cast<double>(std::numeric_limits<qint64>::max()/1000)) deadline(created.addMSecs(static_cast<qint64>(comparison.second*1000)+1)); }
        }
        if (!valid) { matches.clear(); result.errors.append(rule.value["name"].toString()+": regular expression exceeded its resource limit; this rule could not be evaluated."); }
    }
    return result;
}
QJsonObject draft(QString id, QString name, QString field, QString expression, QString color, QString animation, QString sound, QString description) {
    return {{"id", id}, {"name", name}, {"description", description}, {"enabled", true}, {"builtIn", true},
        {"groups", QJsonArray{QJsonArray{QJsonObject{{"field", field}, {"expression", expression}}}}},
        {"color", color}, {"colorMode", "no-match"}, {"colorSeconds", 5}, {"animation", animation}, {"animationMode", animation == "none" ? "no-match" : "new-in-view"}, {"animationSeconds", 5},
        {"zoom", color == "status" ? 100 : 0}, {"sound", sound}, {"soundMinimumMatches", 1}};
}
QVariantList soundCatalog() {
    static const QVariantList catalog = [] {
        const auto sound = [](const QString& id, const QString& name, const QString& purpose,
                              const QString& source, const QString& asset, bool music, const QString& author) {
            return QVariantMap{{"id", id}, {"name", name}, {"purpose", purpose}, {"author", author},
                {"license", "CC0-1.0"}, {"source", source}, {"asset", asset}, {"isMusic", music}};
        };
        QVariantList result{
            sound("none", "No sound", "Silent alert action.", "https://github.com/YunaBraska/podlord", "", false, "Podlord project"),
            sound("panel-segment-load", "Panel segment load", "Short low-power tick for health bar and panel segment changes.", "https://kenney.nl/assets/ui-audio", "qrc:/podlord/audio/ui/panel-segment-load.ogg", false, "Kenney"),
            sound("radar-activated", "Radar activated", "Scanner wake-up sound when the radar leaves idle screensaver mode.", "https://kenney.nl/assets/interface-sounds", "qrc:/podlord/audio/events/radar-activated.ogg", false, "Kenney"),
            sound("activity-tick", "Activity tick", "Small non-alarming activity cue for recent changes.", "https://kenney.nl/assets/interface-sounds", "qrc:/podlord/audio/events/activity-tick.ogg", false, "Kenney"),
            sound("warning-ping", "Warning ping", "Amber tactical warning cue.", "https://kenney.nl/assets/interface-sounds", "qrc:/podlord/audio/alerts/warning-ping.ogg", false, "Kenney"),
            sound("electro-warning", "Electro warning", "Bright electronic warning chirp.", "https://kenney.nl/assets/digital-audio", "qrc:/podlord/audio/alerts/electro-warning.ogg", false, "Kenney"),
            sound("bell-alert", "Bell alert", "Metallic command bell for notable changes.", "https://kenney.nl/assets/impact-sounds", "qrc:/podlord/audio/alerts/bell-alert.ogg", false, "Kenney"),
            sound("metal-impact", "Metal impact", "Industrial metal impact for heavy alerts.", "https://kenney.nl/assets/impact-sounds", "qrc:/podlord/audio/alerts/metal-impact.ogg", false, "Kenney"),
            sound("critical-klaxon", "Critical klaxon", "Short red alert cue for critical matches.", "https://kenney.nl/assets/sci-fi-sounds", "qrc:/podlord/audio/alerts/critical-klaxon.ogg", false, "Kenney"),
            sound("power-up", "Power up", "Positive tactical activation sound.", "https://kenney.nl/assets/digital-audio", "qrc:/podlord/audio/events/power-up.ogg", false, "Kenney"),
            sound("power-down", "Power down", "Low descending tactical cue.", "https://kenney.nl/assets/digital-audio", "qrc:/podlord/audio/events/power-down.ogg", false, "Kenney"),
            sound("three-tone", "Three tone", "Neutral three-tone system notification.", "https://kenney.nl/assets/digital-audio", "qrc:/podlord/audio/events/three-tone.ogg", false, "Kenney"),
            sound("metal-click", "Metal click", "Tiny mechanical UI click.", "https://kenney.nl/assets/rpg-audio", "qrc:/podlord/audio/ui/metal-click.ogg", false, "Kenney"),
            sound("book-open", "Book open", "Soft fantasy war-room page cue.", "https://kenney.nl/assets/rpg-audio", "qrc:/podlord/audio/fantasy/book-open.ogg", false, "Kenney"),
            sound("command-ambient-loop", "Command jingle", "Short optional retro command jingle for demos.", "https://kenney.nl/assets/music-jingles", "qrc:/podlord/audio/music/energetic/command-jingle.ogg", true, "Kenney"),
            sound("steel-command-jingle", "Steel command jingle", "Short metallic command jingle.", "https://kenney.nl/assets/music-jingles", "qrc:/podlord/audio/music/energetic/steel-command.ogg", true, "Kenney"),
            sound("bit-command-jingle", "8-bit command jingle", "Short retro digital command jingle.", "https://kenney.nl/assets/music-jingles", "qrc:/podlord/audio/music/energetic/bit-command.ogg", true, "Kenney")
        };
        for (const auto& file : QDir(":/podlord/audio/interface").entryList({"*.ogg"}, QDir::Files, QDir::Name)) {
            QString base = file.chopped(4);
            auto parts = base.split('_');
            for (auto& part : parts) if (!part.isEmpty()) part[0] = part[0].toUpper();
            result.append(sound("kenney-interface-" + base.replace('_', '-'), parts.join(' '),
                "Interface command sound from Kenney Interface Sounds.", "https://kenney.nl/assets/interface-sounds",
                "qrc:/podlord/audio/interface/" + file, false, "Kenney"));
        }
        return result;
    }();
    return catalog;
}
}
Result<AlertRule> parseAlertRule(QJsonObject value) {
    const QStringList keys{"id", "name", "description", "enabled", "builtIn", "groups", "color", "colorMode", "colorSeconds", "animation", "animationMode", "animationSeconds", "zoom", "sound", "soundMinimumMatches"};
    if (value.size() != keys.size()) return Failure{StoreError::InvalidInput, "An alert must contain exactly the supported fields."};
    for (const auto& key : keys) if (!value.contains(key)) return Failure{StoreError::InvalidInput, "An alert field is missing."};
    if (!value["enabled"].isBool() || !value["builtIn"].isBool() || !value["id"].isString() || value["id"].toString().isEmpty() || !value["name"].isString() || value["name"].toString().trimmed().isEmpty() || !value["description"].isString()) return Failure{StoreError::InvalidInput, "Provide an alert name and valid identity."};
    if (!value["color"].isString() || (!QStringList{"none", "status", "fresh"}.contains(value["color"].toString()) && !(value["color"].toString().startsWith('#') && QColor(value["color"].toString()).isValid()))
        || !modes.contains(value["colorMode"].toString()) || !modes.contains(value["animationMode"].toString())
        || !QStringList{"none", "blink", "pulse", "sweep", "outline"}.contains(value["animation"].toString())
        || !whole(value["colorSeconds"], 1, 60) || !whole(value["animationSeconds"], 1, 60) || !whole(value["zoom"], 0, 200) || !whole(value["soundMinimumMatches"], 1, std::numeric_limits<int>::max())) return Failure{StoreError::InvalidInput, "Choose valid color, animation, hold duration and zoom actions."};
    bool sound = false; for (const auto& item : soundCatalog()) sound = sound || item.toMap()["id"].toString() == value["sound"].toString();
    if (!sound) return Failure{StoreError::InvalidInput, "Choose an available local sound."};
    if (!value["groups"].isArray() || value["groups"].toArray().isEmpty()) return Failure{StoreError::InvalidInput, "Add at least one matcher group."};
    AlertRule rule{value, {}};
    static const QRegularExpression tokenPattern("\"[^\"]*\"|/[^/]*/|[^\\s]+"), comparison("^(<=|>=|>|<|=)?(.*)$");
    for (const auto& group : value["groups"].toArray()) {
        if (!group.isArray() || group.toArray().isEmpty()) return Failure{StoreError::InvalidInput, "Every OR group needs at least one AND criterion."};
        QList<AlertCriterion> criteria;
        for (const auto& input : group.toArray()) {
            const auto object=input.toObject(); const auto field=object["field"].toString(), expression=object["expression"].toString().trimmed();
            if (!input.isObject() || object.size()!=2 || !scopes.contains(field) || !object["expression"].isString() || expression.isEmpty()) return Failure{StoreError::InvalidInput, "Choose a supported field and nonempty matcher expression."};
            AlertCriterion criterion{field, expression, {}, {}, {}};
            if (QStringList{"problems", "activity", "recentlyChanged", "newInView"}.contains(field) && expression != "true" && expression != "false") return Failure{StoreError::InvalidInput, "Boolean matchers accept true or false."};
            auto tokens = tokenPattern.globalMatch(expression);
            while (tokens.hasNext()) {
                const auto token=tokens.next().captured(); QRegularExpression pattern;
                if (numeric(field) && expression != "outlier" && expression != "p95") {
                    const auto parsed=comparison.match(token); const auto expected=quantity(parsed.captured(2), field);
                    if (!expected) return Failure{StoreError::InvalidInput, "Invalid numeric or duration matcher."};
                    criterion.comparisons.append({parsed.captured(1), *expected});
                } else if (token.startsWith('/')) {
                    if (!token.endsWith('/') || token.size()<3) return Failure{StoreError::InvalidInput, "A regular expression must be enclosed in slashes."};
                    pattern=QRegularExpression("(*LIMIT_MATCH=100000)(*LIMIT_DEPTH=1000)"+token.mid(1, token.size()-2), QRegularExpression::CaseInsensitiveOption);
                    if (!pattern.isValid()) return Failure{StoreError::InvalidInput, "Invalid regular expression."};
                } else if (token.startsWith('"') && !token.endsWith('"')) return Failure{StoreError::InvalidInput, "An exact matcher must have closing quotes."};
                criterion.tokens.append(token); criterion.patterns.append(pattern);
            }
            criteria.append(criterion);
        }
        rule.groups.append(criteria);
    }
    return rule;
}
namespace {
Result<AlertCatalog> defaultAlerts() {
    auto active=draft("default-active-view-pulse", "Active view pulse", "newInView", "true", "none", "pulse", "none", "Pulse active resources briefly when they enter the view.");
    active["groups"]=QJsonArray{QJsonArray{QJsonObject{{"field", "newInView"}, {"expression", "true"}}, QJsonObject{{"field", "activity"}, {"expression", "true"}}}};
    AlertCatalog result;
    for (const auto& value : {
        draft("default-problem-color", "Problem color", "problems", "true", "status", "none", "warning-ping", "Paint resources yellow or red while they have an active problem."),
        draft("default-recent-change-color", "Recent change color", "recentlyChanged", "true", "fresh", "none", "none", "Highlight recently changed resources green for the freshness window."),
        active}) {
        const auto parsed = parseAlertRule(value);
        if (const auto* failure = std::get_if<Failure>(&parsed))
            return Failure{StoreError::InvalidData, "Cannot load built-in alert " + value["id"].toString() + ": " + failure->message};
        result.rules.append(std::get<AlertRule>(parsed));
    }
    return result;
}
Result<AlertCatalog> decodeAlerts(const QJsonObject& root) {
    if (root.size()!=4 || root["version"]!=1 || !root["rules"].isArray() || !root["muted"].isBool() || !root["reducedMotion"].isBool()) return Failure{StoreError::InvalidData, "Invalid or unsupported alert document; existing data was retained."};
    const auto defaults = defaultAlerts();
    if (const auto* failure = std::get_if<Failure>(&defaults)) return *failure;
    auto result=std::get<AlertCatalog>(defaults); result.muted=root["muted"].toBool(); result.reducedMotion=root["reducedMotion"].toBool(); QSet<QString> ids;
    for (const auto& entry : root["rules"].toArray()) {
        if (!entry.isObject()) return Failure{StoreError::InvalidData, "Invalid alert record; existing data was retained."};
        const auto parsed=parseAlertRule(entry.toObject()); if (const auto* failure=std::get_if<Failure>(&parsed)) return *failure;
        auto rule=std::get<AlertRule>(parsed); const auto id=rule.value["id"].toString();
        if (ids.contains(id)) return Failure{StoreError::InvalidData, "Duplicate alert identity; existing data was retained."}; ids.insert(id);
        auto builtin=std::find_if(result.rules.begin(), result.rules.end(), [&](const auto& item) { return item.value["id"] == id; });
        if (builtin!=result.rules.end()) {
            const QJsonValue enabled=rule.value["enabled"]; rule.value["enabled"]=builtin->value["enabled"];
            if (rule.value["description"]=="Built-in desktop alert") rule.value["description"]=builtin->value["description"];
            if (rule.value!=builtin->value) return Failure{StoreError::InvalidData, "Built-in alert definitions are locked."}; builtin->value["enabled"]=enabled;
        } else {
            if (rule.value["builtIn"].toBool()) return Failure{StoreError::InvalidData, "Unknown built-in alert; existing data was retained."}; result.rules.append(rule);
        }
    }
    return result;
}
}
Result<AlertCatalog> AlertStore::load() const {
    if (const auto failure=profileFailure(profile_)) return *failure;
    const auto path=QDir(profile_).filePath("alert-rules.json");
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Alert rules must not be a symbolic link."};
    if (!QFileInfo::exists(path)) return defaultAlerts();
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size()>65536) return Failure{StoreError::ReadFailed, "Cannot read private alert rules."};
    const auto bytes=file.readAll(); if (file.error()!=QFileDevice::NoError) return Failure{StoreError::ReadFailed, "Cannot read private alert rules."};
    QJsonParseError error; const auto document=QJsonDocument::fromJson(bytes, &error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) return Failure{StoreError::InvalidData, "Invalid alert document; existing data was retained."};
    return decodeAlerts(document.object());
}
Result<AlertCatalog> AlertStore::save(const AlertCatalog& desired, const AlertCatalog& expected) const {
    const auto validated=decodeAlerts(encoded(desired)); if (const auto* failure=std::get_if<Failure>(&validated)) return *failure;
    if (const auto failure=profileFailure(profile_)) return *failure;
    const auto bytes=QJsonDocument(encoded(desired)).toJson();
    if (bytes.size()>65536) return Failure{StoreError::InvalidInput, "Alert rules exceed the private configuration size limit."};
    const bool created=!QFileInfo::exists(profile_);
    if (!QDir().mkpath(profile_) || (created && !QFile::setPermissions(profile_, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner))) return Failure{StoreError::WriteFailed, "Cannot create a private alert profile."};
    const auto path=QDir(profile_).filePath("alert-rules.json"); QLockFile lock(path+".lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error()==QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed, "Cannot lock alert rules; retry explicitly."};
    const auto current=load(); if (const auto* failure=std::get_if<Failure>(&current)) return *failure;
    if (encoded(std::get<AlertCatalog>(current))!=encoded(expected)) return Failure{StoreError::Conflict, "Alert rules changed in another window. Reload before saving."};
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner|QFile::WriteOwner) || file.write(bytes)!=bytes.size() || !file.commit()) return Failure{StoreError::WriteFailed, "Cannot atomically save private alert rules."};
    return std::get<AlertCatalog>(validated);
}
Alerts::Alerts(QString profile, ResourceClient* client, QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), profile_(std::move(profile)), client_(client), now_(std::move(now)) {
    expiry_.setSingleShot(true);
    connect(&expiry_, &QTimer::timeout, this, [this] { for (auto it=states_.cbegin(); it!=states_.cend(); ++it) queue(it.key()); });
    connect(client_, &ResourceClient::rowsChanged, this, [this](const QString& session) { queue(session); });
    connect(client_, &ResourceClient::changed, this, [this](const QString& session) {
        if (session.isEmpty() || closed_.contains(session)) return;
        const bool loading = !client_->initialSyncComplete(session);
        if (states_[session].loading != loading) { states_[session].loading = loading; queue(session); }
    });
    busy_=false; reload();
}
QVariantList Alerts::rules() const { QVariantList result; for (const auto& rule : catalog_.rules) result.append(rule.value.toVariantMap()); return result; }
QVariantList Alerts::matches() const { const auto state=states_.constFind(shown_); return state==states_.cend() ? QVariantList{} : state->matches; }
QString Alerts::evaluationError() const { const auto state=states_.constFind(shown_); return state==states_.cend() ? QString{} : state->evaluationError; }
QStringList Alerts::fields() const { return scopes; }
QVariantList Alerts::sounds() const { return soundCatalog(); }
bool Alerts::copyText(const QString& text) {
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard || text.size() > 65536) return false;
    clipboard->setText(text);
    return true;
}
bool Alerts::reload() {
    if (busy_) return false; busy_=true; emit rulesChanged();
    auto* watcher=new QFutureWatcher<Result<AlertCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<AlertCatalog>>::finished, this, [this, watcher] {
        const auto result=watcher->result(); watcher->deleteLater(); busy_=false;
        if (const auto* failure=std::get_if<Failure>(&result)) { error_=failure->message; ready_=false; }
        else { catalog_=std::get<AlertCatalog>(result); error_.clear(); ready_=true; for (auto it=states_.cbegin(); it!=states_.cend(); ++it) queue(it.key()); }
        emit rulesChanged();
    });
    const auto profile=profile_; watcher->setFuture(QtConcurrent::run([profile] { return AlertStore(profile).load(); })); return true;
}
bool Alerts::persist(AlertCatalog desired) {
    if (busy_ || !ready_) return false;
    busy_=true; emit rulesChanged(); auto* watcher=new QFutureWatcher<Result<AlertCatalog>>(this);
    connect(watcher, &QFutureWatcher<Result<AlertCatalog>>::finished, this, [this, watcher] {
        const auto result=watcher->result(); watcher->deleteLater(); busy_=false;
        if (const auto* failure=std::get_if<Failure>(&result)) error_=failure->message;
        else { catalog_=std::get<AlertCatalog>(result); error_.clear(); if (catalog_.muted && player_) player_->stop(); for (auto it=states_.cbegin(); it!=states_.cend(); ++it) queue(it.key()); }
        emit rulesChanged();
    });
    const auto profile=profile_; const auto expected=catalog_;
    watcher->setFuture(QtConcurrent::run([profile, desired, expected] { return AlertStore(profile).save(desired, expected); })); return true;
}
bool Alerts::saveRule(const QVariantMap& input) {
    auto value=QJsonObject::fromVariantMap(input);
    if (value["id"].toString().isEmpty()) value["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto desired=catalog_; auto found=std::find_if(desired.rules.begin(), desired.rules.end(), [&](const auto& rule) { return rule.value["id"] == value["id"]; });
    if (found!=desired.rules.end() && found->value["builtIn"].toBool()) {
        auto locked=found->value; locked["enabled"]=value["enabled"];
        if (locked!=value) { error_="Built-in alerts can only be enabled or disabled. Duplicate to edit."; emit rulesChanged(); return false; }
    } else if (value["builtIn"].toBool()) { error_="New alerts cannot claim a built-in identity."; emit rulesChanged(); return false; }
    const auto result=parseAlertRule(value); if (const auto* failure=std::get_if<Failure>(&result)) { error_=failure->message; emit rulesChanged(); return false; }
    if (found==desired.rules.end()) desired.rules.append(std::get<AlertRule>(result)); else *found=std::get<AlertRule>(result);
    return persist(desired);
}
bool Alerts::duplicateRule(const QString& id) {
    for (const auto& rule : catalog_.rules) if (rule.value["id"] == id) { auto value=rule.value; value["id"]=""; value["builtIn"]=false; value["name"]=value["name"].toString()+" copy"; return saveRule(value.toVariantMap()); } return false;
}
bool Alerts::deleteRule(const QString& id) {
    auto desired=catalog_; const auto found=std::find_if(desired.rules.begin(), desired.rules.end(), [&](const auto& rule) { return rule.value["id"] == id; });
    if (found==desired.rules.end() || found->value["builtIn"].toBool()) return false; desired.rules.erase(found); return persist(desired);
}
bool Alerts::setPreferences(bool muted, bool reducedMotion) { auto desired=catalog_; desired.muted=muted; desired.reducedMotion=reducedMotion; return persist(desired); }
bool Alerts::showSession(const QString& session) {
    closed_.remove(session); if (shown_==session) return true; shown_=session; ++zoomPreviewGeneration_; zoomPreviewError_.clear(); emit zoomPreviewChanged(); ++revision_; emit presentationChanged(); return true;
}
bool Alerts::closeSession(const QString& session) { closed_.insert(session); pending_.remove(session); states_.remove(session); if (session == shown_) ++zoomPreviewGeneration_; return true; }
QVariantMap Alerts::effect(const QString& path) const { const auto state=states_.constFind(shown_); return state==states_.cend() ? QVariantMap{} : state->effects.value(path); }
bool Alerts::queue(const QString& session) {
    if (session.isEmpty() || closed_.contains(session)) return false;
    ++states_[session].generation; pending_.insert(session); return dispatch();
}
bool Alerts::dispatch() {
    if (evaluating_ || !ready_ || pending_.isEmpty()) return false;
    const auto session=*pending_.cbegin(); pending_.remove(session); auto& state=states_[session];
    if (!client_->initialSyncComplete(session)) {
        const bool changed = !state.matches.isEmpty() || !state.effects.isEmpty();
        state.matches.clear(); state.effects.clear(); state.colorUntil.clear(); state.animationUntil.clear();
        if (changed && session == shown_) { ++revision_; emit presentationChanged(); }
        return dispatch();
    }
    const bool baseline = !state.initialized;
    const auto snapshot=client_->rows(session); const auto now=now_(); QMap<QString,QJsonObject> current;
    for (const auto& value : snapshot) { const auto row=value.toObject(); const auto path=row["path"].toString(); current[path]=row;
        if (!baseline && (!state.previous.contains(path) || state.previous[path]["uid"]!=row["uid"] || state.previous[path]["resourceVersion"]!=row["resourceVersion"])) state.changedAt[path]=now;
    }
    for (auto it=state.changedAt.begin(); it!=state.changedAt.end();) { if (!current.contains(it.key())) it=state.changedAt.erase(it); else ++it; }
    state.previous=current; const auto changes=state.changedAt; const auto catalog=catalog_; const auto generation=state.generation;
    evaluating_=true; auto* watcher=new QFutureWatcher<Evaluated>(this);
    connect(watcher, &QFutureWatcher<Evaluated>::finished, this, [this, watcher, session, generation, catalog, baseline] {
        const auto result=watcher->result(); watcher->deleteLater(); evaluating_=false;
        auto found=states_.find(session);
        if (found==states_.end() || found->generation!=generation || closed_.contains(session)) { dispatch(); return; }
        auto& state=found.value(); const auto now=now_(); QVariantList matches; QMap<QString,QVariantMap> effects; QMap<QString,QJsonObject> triggered; QSet<QString> alive;
        state.initialized = true;
        QDateTime next=result.next;
        const auto deadline=[&](QDateTime at) { if (at>now && (!next.isValid() || at<next)) next=at; };
        for (const auto& rule : catalog.rules) {
            const auto id=rule.value["id"].toString(); bool changed=false; int count=0; QString first, focusPath;
            QDateTime latestChange; int focusSeverity=-1;
            for (const auto& row : result.matches.value(id)) {
                const auto path=row["path"].toString(), key=id+'\n'+path+'\n'+row["uid"].toString(); alive.insert(key); ++count; if (first.isEmpty()) first=path;
                QJsonObject fingerprint{{"status", row["status"]}, {"ready", row["ready"]}, {"restarts", row["restarts"]}, {"issue", row["issue"]}};
                const bool entered=!state.triggered.contains(key) || state.triggered[key]!=fingerprint; changed=changed || entered; triggered[key]=fingerprint;
                const auto changedAt=state.changedAt.value(path);
                const int severity=row["problemSeverity"].toInt();
                if (entered && (focusPath.isEmpty() || changedAt>latestChange
                    || (changedAt==latestChange && (severity>focusSeverity || (severity==focusSeverity && path<focusPath))))) {
                    focusPath=path; latestChange=changedAt; focusSeverity=severity;
                }
                if (entered) { state.colorUntil[key]=now.addSecs(rule.value["colorSeconds"].toInt()); if (!baseline) state.animationUntil[key]=now.addSecs(rule.value["animationSeconds"].toInt()); }
            }
            if (count) matches.append(QVariantMap{{"id", id}, {"name", rule.value["name"].toString()}, {"count", count}, {"path", first}});
            if (!baseline && changed && count>=rule.value["soundMinimumMatches"].toInt() && !catalog.muted && rule.value["sound"]!="none") { emit soundRequested(session, id, rule.value["sound"].toString()); play(rule.value["sound"].toString()); }
            if (!baseline && !focusPath.isEmpty() && rule.value["zoom"].toInt()>0) emit focusRequested(session, focusPath, rule.value["zoom"].toInt());
            if (!rule.value["enabled"].toBool()) continue;
            for (const auto& row : result.rows) {
                const auto path=row["path"].toString(), key=id+'\n'+path+'\n'+row["uid"].toString();
                const bool matching=alive.contains(key);
                const auto active=[&](const QString& action, const QMap<QString,QDateTime>& holds) {
                    const auto mode=rule.value[action+"Mode"].toString(); if (mode=="no-match") return matching;
                    const auto until=holds.value(key); deadline(until); return until>now && (mode=="duration" || matching);
                };
                auto effect=effects.value(path);
                if (rule.value["color"]!="none" && active("color", state.colorUntil) && !(rule.value["color"]=="fresh" && effect.contains("color"))) effect["color"]=rule.value["color"].toString();
                if (rule.value["animation"]!="none" && active("animation", state.animationUntil)) effect["animation"]=rule.value["animation"].toString();
                if (!effect.isEmpty()) effects[path]=effect;
            }
        }
        state.triggered=triggered;
        const auto prune=[&](QMap<QString,QDateTime>& holds) { for (auto it=holds.begin(); it!=holds.end();) { const auto parts=it.key().split('\n'); const bool exists=parts.size()==3 && result.rows.contains(parts[1]) && result.rows[parts[1]]["uid"]==parts[2]; if (!exists || it.value()<=now) it=holds.erase(it); else ++it; } };
        prune(state.colorUntil); prune(state.animationUntil);
        const auto failure=result.errors.join('\n');
        const bool changed=state.matches!=matches || state.effects!=effects || state.evaluationError!=failure; state.matches=matches; state.effects=effects; state.evaluationError=failure;
        if (session==shown_ && changed) { ++revision_; emit presentationChanged(); }
        if (next.isValid()) { const auto remaining=std::clamp<qint64>(now.msecsTo(next), 1, std::numeric_limits<int>::max()); if (!expiry_.isActive() || expiry_.remainingTime()>remaining) expiry_.start(static_cast<int>(remaining)); }
        dispatch();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, catalog, changes, now] { return evaluate(snapshot, catalog, changes, now); })); return true;
}
bool Alerts::play(const QString& id) {
    for (const auto& value : sounds()) { const auto sound=value.toMap(); if (sound["id"].toString()!=id) continue;
        if (id=="none") return true;
        if (!player_) { output_=std::make_unique<QAudioOutput>(); player_=std::make_unique<QMediaPlayer>(); player_->setAudioOutput(output_.get());
            connect(player_.get(), &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString&) { error_="Local alert sound could not be played. Visual alarms remain active."; emit rulesChanged(); }); }
        player_->stop(); player_->setSource(QUrl(sound["asset"].toString())); player_->play(); return true;
    } return false;
}
bool Alerts::previewSound(const QString& id) { return !muted() && play(id); }
bool Alerts::previewZoom(const QVariantMap& draft, const QStringList& visiblePaths) {
    if (zoomPreviewBusy_) return false;
    const auto reject = [this](const QString& message) { zoomPreviewError_ = message; emit zoomPreviewChanged(); return false; };
    if (busy_ || !ready_) return reject("Wait until the alert catalog is ready.");
    if (shown_.isEmpty() || visiblePaths.isEmpty() || closed_.contains(shown_)) return reject("No visible cached resource is available for zoom preview.");
    if (client_->syncLoading(shown_) || !client_->initialSyncComplete(shown_)) return reject("Wait until the session finishes loading.");
    auto value = QJsonObject::fromVariantMap(draft);
    if (value["id"].isString() && value["id"].toString().isEmpty()) value["id"] = "zoom-preview";
    const auto parsed = parseAlertRule(value);
    if (const auto* failure = std::get_if<Failure>(&parsed)) return reject(failure->message);
    auto rule = std::get<AlertRule>(parsed);
    rule.value["enabled"] = true;
    const AlertCatalog catalog{{rule}, false, false};
    const auto session = shown_;
    const auto snapshot = client_->rows(session);
    const auto state = states_.value(session);
    const auto changes = state.changedAt;
    const auto now = now_();
    const auto generation = ++zoomPreviewGeneration_;
    zoomPreviewBusy_ = true; zoomPreviewError_.clear(); emit zoomPreviewChanged();
    auto* watcher = new QFutureWatcher<Evaluated>(this);
    connect(watcher, &QFutureWatcher<Evaluated>::finished, this, [this, watcher, session, generation, cacheGeneration = state.generation, visiblePaths, rule] {
        const auto result = watcher->result(); watcher->deleteLater(); zoomPreviewBusy_ = false;
        if (generation != zoomPreviewGeneration_ || session != shown_ || closed_.contains(session)) { emit zoomPreviewChanged(); return; }
        if (client_->syncLoading(session)) zoomPreviewError_ = "The session is synchronizing. Preview again explicitly after it finishes.";
        else if (states_.value(session).generation != cacheGeneration) zoomPreviewError_ = "The session cache changed. Preview again explicitly.";
        else if (!result.errors.isEmpty()) zoomPreviewError_ = result.errors.join('\n');
        else {
            QSet<QString> matching;
            for (const auto& row : result.matches.value(rule.value["id"].toString())) matching.insert(row["path"].toString());
            QString target;
            for (const auto& path : visiblePaths) {
                if (!result.rows.contains(path)) continue;
                if (target.isEmpty()) target = path;
                if (matching.contains(path)) { target = path; break; }
            }
            if (target.isEmpty()) zoomPreviewError_ = "No visible cached resource is available for zoom preview.";
            else { emit zoomPreviewChanged(); emit zoomPreviewReady(session, target, std::max(100, rule.value["zoom"].toInt())); return; }
        }
        emit zoomPreviewChanged();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, catalog, changes, now] { return evaluate(snapshot, catalog, changes, now); }));
    return true;
}
} // namespace podlord
