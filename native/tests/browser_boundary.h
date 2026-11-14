#pragma once
#include <QDesktopServices>
#include <QList>
#include <QObject>
#include <QUrl>

// Only the external browser handoff is replaced; application controls remain real.
class BrowserBoundary final : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
    BrowserBoundary() { QDesktopServices::setUrlHandler("https", this, "open"); }
    ~BrowserBoundary() override { QDesktopServices::unsetUrlHandler("https"); }
public slots:
    bool open(const QUrl& url) { urls.append(url); return true; }
};
