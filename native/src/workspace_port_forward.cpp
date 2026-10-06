#include "workspace.h"
#include <QTcpServer>
#include <QHostAddress>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>

namespace podlord {
bool Workspace::publishPorts() {
    portRows_.publish(QJsonArray::fromVariantList(client_.portForwards(active_)));
    const auto nav = navigation_.value(active_);
    ports_.setFilterFixedString(nav.portFilter.trimmed()); ports_.sort(nav.portColumn, nav.portOrder); return true;
}
bool Workspace::filterPorts(const QString& text) {
    if (active_.isEmpty()) return false;
    auto& nav = navigation_[active_];
    if (nav.portFilter == text) return true;
    nav.portFilter = text; ports_.setFilterFixedString(text.trimmed()); queueViewSave("port"); emit changed(); return true;
}
bool Workspace::copyPortValue(const QString& token, int column) {
    if (!QGuiApplication::clipboard() || column < 0 || column >= portRows_.columnCount()) return false;
    for (int row = 0; row < portRows_.rowCount(); ++row) {
        if (portRows_.data(portRows_.index(row, 0), Qt::UserRole).toString() != token) continue;
        QGuiApplication::clipboard()->setText(portRows_.data(portRows_.index(row, column)).toString()); return true;
    }
    return false;
}
bool Workspace::canPortForward() const {
    const auto row = client_.resource(active_, inspectorPath());
    return !busy_ && !authenticationRequired() && !authenticationRunning() && client_.containsResource(active_, inspectorPath())
        && row["apiVersion"] == "v1" && !row["namespace"].toString().isEmpty() && !row["uid"].toString().isEmpty()
        && (row["kind"] == "Service" || (row["kind"] == "Pod" && row["status"] == "Running"));
}
bool Workspace::preparePortForward() {
    if (!canPortForward()) return false;
    const auto row = client_.resource(active_, inspectorPath());
    int remote = 8080;
    const auto spec = inspectorDocument_["spec"].toObject();
    const auto containers = spec["containers"].toArray();
    const auto ports = row["kind"] == "Service" ? spec["ports"].toArray()
        : (containers.isEmpty() ? QJsonArray{} : containers.first().toObject()["ports"].toArray());
    for (const auto& value : ports) {
        const auto port = value.toObject(); const auto n = port[row["kind"] == "Service" ? "port" : "containerPort"].toInt();
        if (n > 0 && n <= 65535 && port["protocol"].toString("TCP") == "TCP") { remote = n; break; }
    }
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, static_cast<quint16>(remote)) && !probe.listen(QHostAddress::LocalHost)) {
        error_ = "No local loopback port could be reserved."; emit changed(); return false;
    }
    int local = probe.serverPort(); probe.close();
    QString existing;
    for (const auto& value : client_.portForwards(active_)) {
        const auto forward = value.toMap();
        if (forward["path"] == inspectorPath() && forward["uid"].toString() == row["uid"].toString()) {
            existing = forward["id"].toString(); local = forward["localPort"].toInt(); remote = forward["remotePort"].toInt(); break;
        }
    }
    portForwardTarget_ = {{"session", active_}, {"path", inspectorPath()}, {"uid", row["uid"].toString()}, {"kind", row["kind"].toString()},
        {"name", row["name"].toString()}, {"namespace", row["namespace"].toString()}, {"title", title()}, {"localPort", local}, {"remotePort", remote}, {"existingId", existing}};
    error_.clear(); emit changed(); return true;
}
bool Workspace::cancelPreparedPortForward() { portForwardTarget_.clear(); emit changed(); return true; }
bool Workspace::startPreparedPortForward(const QString& local, const QString& remote) {
    const auto target = portForwardTarget_;
    if (target.isEmpty() || !target["existingId"].toString().isEmpty() || target["session"] != active_
        || target["path"] != inspectorPath() || client_.resource(active_, inspectorPath())["uid"].toString() != target["uid"].toString()) {
        error_ = "The prepared port-forward target is no longer selected. Review the target again."; emit changed(); return false;
    }
    static const QRegularExpression number("^[0-9]{1,5}$");
    const int a = number.match(local).hasMatch() ? local.toInt() : 0, b = number.match(remote).hasMatch() ? remote.toInt() : 0;
    if (a < 1 || a > 65535 || b < 1 || b > 65535) { error_ = "Both TCP ports must be whole numbers from 1 through 65535."; emit changed(); return false; }
    const auto result = client_.startPortForward(active_, inspectorPath(), a, b);
    if (const auto* failure = std::get_if<Failure>(&result)) { error_ = failure->message; emit changed(); return false; }
    portForwardTarget_.clear(); error_.clear(); emit changed(); return true;
}
bool Workspace::stopPreparedPortForward() {
    if (portForwardTarget_["session"] != active_) return false;
    const auto token = portForwardTarget_["existingId"].toString();
    if (!client_.stopPortForward(active_, token)) return false;
    return cancelPreparedPortForward();
}
bool Workspace::stopPortForward(const QString& token) { return client_.stopPortForward(active_, token); }
bool Workspace::copyPortForwardEndpoint(const QString& token) {
    if (!QGuiApplication::clipboard()) return false;
    for (const auto& value : client_.portForwards(active_)) {
        const auto forward=value.toMap();
        if (forward["id"]==token) {
            QGuiApplication::clipboard()->setText(forward["endpoint"].toString());
            return true;
        }
    }
    return false;
}
bool Workspace::openPortForwardEndpoint(const QString& token, bool secure) {
    for (const auto& value : client_.portForwards(active_)) {
        const auto forward = value.toMap();
        if (forward["id"] != token || forward["status"] != "Listening") continue;
        QUrl url; url.setScheme(secure ? "https" : "http"); url.setHost("127.0.0.1");
        url.setPort(forward["localPort"].toInt()); url.setPath("/");
        if (!QDesktopServices::openUrl(url)) {
            error_ = "The browser could not open this local endpoint. Copy it and open it explicitly.";
            emit changed(); return false;
        }
        error_.clear(); emit changed(); return true;
    }
    error_ = "This port forward is no longer listening in the active session.";
    emit changed(); return false;
}
}
