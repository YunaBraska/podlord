#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QPointer>
#include <QQuickWindow>

namespace podlord {
class Workspace;
/** One QML engine; each native window has its own context and workspace projection. */
class WindowHost final : public QObject {
    Q_OBJECT
public:
    explicit WindowHost(Workspace& primary, QObject* parent = nullptr);
    ~WindowHost() override;
    bool open();
    bool detach(Workspace& source, const QString& session);
    bool focus(Workspace& workspace);
    QList<QQuickWindow*> windows() const;
    bool applicationClosing() const;
signals:
    void windowOpened(QQuickWindow* window);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    enum class ClosePhase { Idle, Waiting, Closing };
    ClosePhase closePhase_ = ClosePhase::Idle;
    struct Window final {
        Workspace* workspace;
        QPointer<QQuickWindow> window;
        QQmlContext* context;
        bool owned;
    };
    Workspace& primary_;
    QQmlEngine engine_;
    QQmlComponent component_;
    QList<Window> windows_;
    bool create(Workspace& workspace, bool owned);
    bool retire(Workspace& workspace);
};
}
