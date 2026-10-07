#pragma once
#include <QQuickPaintedItem>
#include <QWebSocket>
#include <QPointer>
#include <QVariantMap>
#include <deque>
#include <vterm.h>

namespace podlord {
/** Session-owned PTY screen and Kubernetes exec transport. No local process or browser. */
class ContainerTerminal final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString target READ target CONSTANT)
public:
    ContainerTerminal(QString target, QObject* parent = nullptr);
    ~ContainerTerminal() override;
    QString status() const { return status_; }
    QString target() const { return target_; }
    bool connected() const { return socket_.state() == QAbstractSocket::ConnectedState && admitted_ && !ended_; }
    bool active() const { return !ended_; }
    bool stop(const QString& reason = "Disconnected by user.");
    bool reject(const QString& reason);
    bool resize(int rows, int columns);
    Q_INVOKABLE bool key(int key, const QString& text, Qt::KeyboardModifiers modifiers);
    Q_INVOKABLE bool paste(const QString& text);
    bool mouse(int row, int column, int button, bool pressed, Qt::KeyboardModifiers modifiers);
    QString text(int scroll = 0) const;
    VTermScreenCell cell(int row, int column, int scroll = 0) const;
    QColor color(VTermColor value) const;
    int rows() const { return rows_; }
    int columns() const { return columns_; }
    int historyLines() const { return static_cast<int>(history_.size()); }
    VTermPos cursor() const;
    bool cursorVisible() const { return cursorVisible_; }
    bool mouseTracking() const { return mouseTracking_; }
signals:
    void changed();
    void screenChanged(int scrolledLines);
    void handshakeFinished(bool accepted, bool authenticationRequired);
private:
    friend class ResourceClient;
    bool open(QNetworkRequest request, const QString& container, const QString& shell);
    VTerm* term_;
    VTermScreen* screen_;
    QWebSocket socket_;
    QString target_, status_ = "Checking selected Pod...";
    QByteArray remoteStatus_;
    QByteArray* outputBatch_ = nullptr;
    std::deque<QVector<VTermScreenCell>> history_;
    qint64 historyBytes_ = 0;
    int rows_ = 24, columns_ = 80, scrolledLines_ = 0;
    bool admitted_ = false, reported_ = false, ended_ = false, cursorVisible_ = true, alternate_ = false, mouseTracking_ = false;
    bool outputClosed_ = false, statusClosed_ = false;
    bool send(int channel, const QByteArray& bytes);
    bool completeStatus();
    bool report(bool accepted, bool auth = false);
    static int property(VTermProp prop, VTermValue* value, void* user);
    static int push(int columns, const VTermScreenCell* cells, void* user);
    static int pop(int columns, VTermScreenCell* cells, void* user);
    static int clear(void* user);
    static void output(const char* bytes, size_t length, void* user);
};

/** Read-only paint surface; explicit keyboard/mouse actions alone send terminal input. */
class TerminalSurface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(podlord::ContainerTerminal* terminal READ terminal WRITE setTerminal NOTIFY terminalChanged)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY colorsChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY colorsChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY colorsChanged)
    Q_PROPERTY(QString visibleText READ visibleText NOTIFY visibleTextChanged)
public:
    explicit TerminalSurface(QQuickItem* parent = nullptr);
    ContainerTerminal* terminal() const { return terminal_; }
    bool setTerminal(ContainerTerminal* terminal);
    QColor foreground() const { return foreground_; }
    QColor background() const { return background_; }
    QString fontFamily() const { return font_.family(); }
    bool setForeground(QColor color);
    bool setBackground(QColor color);
    bool setFontFamily(const QString& family);
    QString visibleText() const { return terminal_ ? terminal_->text(scroll_) : QString{}; }
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE QVariantMap clipboardForPaste() const;
    Q_INVOKABLE bool followOutput();
    void paint(QPainter* painter) override;
signals:
    void terminalChanged();
    void colorsChanged();
    void visibleTextChanged();
    void pasteRequested();
    void controlsFocusRequested();
protected:
    bool event(QEvent* event) override;
    void geometryChange(const QRectF& next, const QRectF& old) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
private:
    QPointer<ContainerTerminal> terminal_;
    QColor foreground_ = Qt::white, background_ = Qt::black;
    QFont font_;
    qreal cellWidth_ = 8, cellHeight_ = 16, ascent_ = 12;
    int scroll_ = 0, selectionStart_ = -1, selectionEnd_ = -1;
    bool selecting_ = false;
    bool sizeScreen();
    int position(const QPointF& point) const;
};
}
