#include "container_terminal.h"
#include <QAuthenticator>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QNetworkProxy>
#include <QUrlQuery>
#include <QWebSocketHandshakeOptions>
#include <QLoggingCategory>
#include <algorithm>
#include <utility>

namespace podlord {
Q_LOGGING_CATEGORY(terminalLog, "podlord.terminal", QtWarningMsg)
namespace {
constexpr qint64 streamLimit = 1024 * 1024, historyLimit = 5 * 1024 * 1024;
#ifdef Q_OS_MACOS
constexpr auto guiModifier = Qt::ControlModifier;
#else
constexpr auto guiModifier = Qt::MetaModifier;
#endif
VTermModifier modifiers(Qt::KeyboardModifiers flags) {
    int value = VTERM_MOD_NONE;
    if (flags & Qt::ShiftModifier) value |= VTERM_MOD_SHIFT;
    if (flags & Qt::AltModifier) value |= VTERM_MOD_ALT;
    // Qt maps the physical Control key to Meta on macOS.
#ifdef Q_OS_MACOS
    if (flags & Qt::MetaModifier) value |= VTERM_MOD_CTRL;
#else
    if (flags & Qt::ControlModifier) value |= VTERM_MOD_CTRL;
#endif
    return static_cast<VTermModifier>(value);
}
QString characters(const VTermScreenCell& cell) {
    QString result;
    if (cell.width == 0 || cell.chars[0] == UINT32_MAX) return result;
    for (const auto c : cell.chars) { if (!c) break; const char32_t value = c; result += QString::fromUcs4(&value, 1); }
    return result.isEmpty() ? QString(" ") : result;
}
}
ContainerTerminal::ContainerTerminal(QString target, QObject* parent) : QObject(parent), term_(vterm_new(24, 80)), target_(std::move(target)) {
    vterm_set_utf8(term_, 1);
    screen_ = vterm_obtain_screen(term_);
    static const VTermScreenCallbacks callbacks{nullptr, nullptr, nullptr, property, nullptr, nullptr, push, pop, clear};
    vterm_screen_set_callbacks(screen_, &callbacks, this);
    vterm_output_set_callback(term_, output, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCREEN);
    vterm_screen_reset(screen_, 1);
    socket_.setProxy(QNetworkProxy::NoProxy);
    socket_.setReadBufferSize(streamLimit + 64);
    socket_.setMaxAllowedIncomingFrameSize(streamLimit + 1);
    socket_.setMaxAllowedIncomingMessageSize(streamLimit + 1);
    connect(&socket_, &QWebSocket::connected, this, [this] {
        if (socket_.subprotocol() != "v5.channel.k8s.io") { reject("The API did not negotiate Kubernetes exec v5. No fallback or retry was sent."); return; }
        admitted_ = true; status_ = "Connected"; report(true); emit changed(); resize(rows_, columns_);
    });
    connect(&socket_, &QWebSocket::authenticationRequired, this, [this](QAuthenticator*) {
        report(false, true); reject("Authentication required. Confirm login explicitly; the shell was not retried.");
    });
    connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        qCDebug(terminalLog) << "Transport failure" << error << "close code" << socket_.closeCode() << "status bytes" << remoteStatus_.size();
        // Kubernetes can close TCP after its authoritative exit status, without a WS close frame.
        if (error == QAbstractSocket::RemoteHostClosedError && (!remoteStatus_.isEmpty() || statusClosed_)) return;
        if (!ended_) reject("Exec connection, TLS or authorization failed. Check access and retry explicitly.");
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString&) { reject("Invalid non-binary Kubernetes exec response."); });
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& message) {
        if (ended_) return;
        if (message.isEmpty()) { reject("Empty Kubernetes exec frame."); return; }
        const auto channel = static_cast<unsigned char>(message[0]);
        const auto payload = message.sliced(1);
        qCDebug(terminalLog) << "Received channel" << channel << "bytes" << payload.size();
        if (channel == 1 || channel == 2) {
            vterm_input_write(term_, payload.constData(), static_cast<size_t>(payload.size()));
            vterm_screen_flush_damage(screen_);
            emit screenChanged(std::exchange(scrolledLines_, 0));
        } else if (channel == 3) {
            if (remoteStatus_.size() + payload.size() > 65536) { reject("Exec status exceeded the bounded response limit."); return; }
            remoteStatus_ += payload;
        } else if (channel == 255 && payload.size() == 1 && static_cast<unsigned char>(payload[0]) <= 4) {
            if (payload[0] == 1) outputClosed_ = true;
            if (payload[0] == 3) statusClosed_ = true;
            if (outputClosed_ && statusClosed_) completeStatus();
        } else reject("Invalid Kubernetes exec channel or close signal.");
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        if (ended_) return;
        if (!remoteStatus_.isEmpty() || statusClosed_ || (admitted_ && socket_.closeCode() == QWebSocketProtocol::CloseCodeNormal)) { completeStatus(); return; }
        reject(admitted_ ? "Connection closed without an exit status. No shell was restarted." : "Connection closed before exec negotiation.");
    });
}
ContainerTerminal::~ContainerTerminal() { disconnect(&socket_, nullptr, this, nullptr); socket_.abort(); vterm_free(term_); }
bool ContainerTerminal::report(bool accepted, bool auth) {
    if (reported_) return false;
    reported_ = true; emit handshakeFinished(accepted, auth); return true;
}
bool ContainerTerminal::open(QNetworkRequest request, const QString& container, const QString& shell) {
    if (ended_ || admitted_ || socket_.state() != QAbstractSocket::UnconnectedState) return false;
    auto url = request.url(); url.setScheme(url.scheme() == "https" ? "wss" : "ws");
    QUrlQuery query; query.addQueryItem("container", container);
    query.addQueryItem("stdin", "true"); query.addQueryItem("stdout", "true"); query.addQueryItem("stderr", "false"); query.addQueryItem("tty", "true");
    query.addQueryItem("command", shell); query.addQueryItem("command", "-c");
    query.addQueryItem("command", "TERM=xterm-256color; export TERM; exec \"$0\" -i"); query.addQueryItem("command", shell);
    url.setQuery(query); request.setUrl(url); request.setRawHeader("Accept", "*/*");
    auto tls = request.sslConfiguration(); tls.setAllowedNextProtocols({QSslConfiguration::NextProtocolHttp1_1}); socket_.setSslConfiguration(tls);
    QWebSocketHandshakeOptions options; options.setSubprotocols({"v5.channel.k8s.io"});
    status_ = "Connecting..."; emit changed(); socket_.open(request, options); return true;
}
bool ContainerTerminal::stop(const QString& reason) {
    if (ended_) return false;
    ended_ = true; status_ = reason; report(false); socket_.abort(); emit changed(); return true;
}
bool ContainerTerminal::reject(const QString& reason) { return stop(reason); }
bool ContainerTerminal::send(int channel, const QByteArray& bytes) {
    if (!connected()) return false;
    if (bytes.size() > streamLimit || socket_.bytesToWrite() + bytes.size() + 1 > streamLimit) return reject("Terminal input exceeded its bounded queue. The connection was closed; input was not retried.");
    if (socket_.sendBinaryMessage(QByteArray(1, static_cast<char>(channel)) + bytes) != bytes.size() + 1) return reject("Terminal input could not be sent. Nothing was retried.");
    return true;
}
bool ContainerTerminal::completeStatus() {
    if (remoteStatus_.isEmpty()) return stop("Shell exited successfully.");
    QJsonParseError error; const auto parsed = QJsonDocument::fromJson(remoteStatus_, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) return reject("Invalid Kubernetes exec exit status.");
    const auto status = parsed.object();
    if (status["status"] == "Success") return stop("Shell exited successfully.");
    if (status["status"] == "Failure" && status["reason"] == "NonZeroExitCode") {
        for (const auto& entry : status["details"].toObject()["causes"].toArray()) {
            const auto cause = entry.toObject(); bool valid = false; const auto code = cause["message"].toString().toUInt(&valid);
            if (cause["reason"] == "ExitCode" && valid && code > 0 && code <= 255) return stop(QString("Shell exited with code %1.").arg(code));
        }
        return reject("Invalid Kubernetes exec exit code.");
    }
    return reject("The API rejected the shell. Check the selected shell path, container and exec permissions.");
}
bool ContainerTerminal::resize(int rows, int columns) {
    if (rows < 2 || rows > 120 || columns < 2 || columns > 320) return false;
    rows_ = rows; columns_ = columns; vterm_set_size(term_, rows, columns); vterm_screen_flush_damage(screen_);
    emit screenChanged(std::exchange(scrolledLines_, 0));
    if (connected()) return send(4, QJsonDocument(QJsonObject{{"Width", columns}, {"Height", rows}}).toJson(QJsonDocument::Compact));
    return !ended_;
}
bool ContainerTerminal::key(int key, const QString& text, Qt::KeyboardModifiers flags) {
    if (!connected()) return false;
    const auto mod = modifiers(flags); VTermKey terminalKey = VTERM_KEY_NONE;
    switch (key) {
    case Qt::Key_Return: case Qt::Key_Enter: terminalKey = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab: case Qt::Key_Backtab: terminalKey = VTERM_KEY_TAB; break;
    case Qt::Key_Backspace: terminalKey = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape: terminalKey = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up: terminalKey = VTERM_KEY_UP; break;
    case Qt::Key_Down: terminalKey = VTERM_KEY_DOWN; break;
    case Qt::Key_Left: terminalKey = VTERM_KEY_LEFT; break;
    case Qt::Key_Right: terminalKey = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert: terminalKey = VTERM_KEY_INS; break;
    case Qt::Key_Delete: terminalKey = VTERM_KEY_DEL; break;
    case Qt::Key_Home: terminalKey = VTERM_KEY_HOME; break;
    case Qt::Key_End: terminalKey = VTERM_KEY_END; break;
    case Qt::Key_PageUp: terminalKey = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown: terminalKey = VTERM_KEY_PAGEDOWN; break;
    default: if (key >= Qt::Key_F1 && key <= Qt::Key_F35) terminalKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    }
    if (terminalKey != VTERM_KEY_NONE) vterm_keyboard_key(term_, terminalKey, mod);
    // The advertised xterm PTY expects C0 controls, not unnegotiated CSI-u sequences.
    else if ((mod & VTERM_MOD_CTRL) && ((key >= Qt::Key_A && key <= Qt::Key_Underscore) || key == Qt::Key_Space))
        vterm_keyboard_unichar(term_, static_cast<uint32_t>(key & 0x1f), static_cast<VTermModifier>(mod & VTERM_MOD_ALT));
    else for (const char32_t c : text.toUcs4()) {
        // libvterm 0.3.3 truncates Alt+Unicode; retain its ESC prefix with valid UTF-8.
        if ((mod & VTERM_MOD_ALT) && !(mod & VTERM_MOD_CTRL) && c > 0x7f) {
            if (!send(0, QByteArray("\033") + QString::fromUcs4(&c, 1).toUtf8())) return false;
        } else vterm_keyboard_unichar(term_, c, mod);
    }
    return terminalKey != VTERM_KEY_NONE || !text.isEmpty() || (mod & VTERM_MOD_CTRL);
}
bool ContainerTerminal::paste(const QString& text) {
    if (!connected() || text.isEmpty() || text.size() > 65536 || text.toUtf8().size() > 65536) return false;
    QByteArray batch;
    batch.reserve(text.size() * 3 + 12);
    outputBatch_ = &batch;
    vterm_keyboard_start_paste(term_);
    for (const auto c : text.toUcs4()) vterm_keyboard_unichar(term_, c, VTERM_MOD_NONE);
    vterm_keyboard_end_paste(term_);
    outputBatch_ = nullptr;
    return send(0, batch);
}
bool ContainerTerminal::mouse(int row, int column, int button, bool pressed, Qt::KeyboardModifiers flags) {
    if (!connected() || !mouseTracking_ || row < 0 || column < 0 || row >= rows_ || column >= columns_) return false;
    vterm_mouse_move(term_, row, column, modifiers(flags));
    if (button) vterm_mouse_button(term_, button, pressed, modifiers(flags));
    return true;
}
VTermPos ContainerTerminal::cursor() const { VTermPos value{}; vterm_state_get_cursorpos(vterm_obtain_state(term_), &value); return value; }
QColor ContainerTerminal::color(VTermColor value) const { vterm_screen_convert_color_to_rgb(screen_, &value); return QColor(value.rgb.red, value.rgb.green, value.rgb.blue); }
VTermScreenCell ContainerTerminal::cell(int row, int column, int scroll) const {
    VTermScreenCell value{};
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_) return value;
    const int offset = alternate_ ? 0 : std::clamp(scroll, 0, historyLines());
    const int index = historyLines() + row - offset;
    if (index < historyLines()) {
        const auto& line = history_[static_cast<size_t>(index)]; if (column < line.size()) value = line[column];
    } else vterm_screen_get_cell(screen_, {row - offset, column}, &value);
    return value;
}
QString ContainerTerminal::text(int scroll) const {
    QString result;
    for (int row = 0; row < rows_; ++row) {
        QString line; for (int column = 0; column < columns_; ++column) line += characters(cell(row, column, scroll));
        while (line.endsWith(' ')) line.chop(1);
        result += line; if (row + 1 < rows_) result += '\n';
    }
    return result;
}
int ContainerTerminal::property(VTermProp prop, VTermValue* value, void* user) {
    auto* self = static_cast<ContainerTerminal*>(user);
    if (prop == VTERM_PROP_CURSORVISIBLE) self->cursorVisible_ = value->boolean;
    if (prop == VTERM_PROP_ALTSCREEN) self->alternate_ = value->boolean;
    if (prop == VTERM_PROP_MOUSE) self->mouseTracking_ = value->number != VTERM_PROP_MOUSE_NONE;
    // Remote titles, OSC clipboard requests and links never change application state.
    return 1;
}
int ContainerTerminal::push(int columns, const VTermScreenCell* cells, void* user) {
    auto* self = static_cast<ContainerTerminal*>(user);
    self->history_.emplace_back(cells, cells + columns); self->historyBytes_ += columns * static_cast<qint64>(sizeof(VTermScreenCell));
    ++self->scrolledLines_;
    while (self->historyBytes_ > historyLimit) { self->historyBytes_ -= self->history_.front().size() * static_cast<qint64>(sizeof(VTermScreenCell)); self->history_.pop_front(); }
    return 1;
}
int ContainerTerminal::pop(int columns, VTermScreenCell* cells, void* user) {
    auto* self = static_cast<ContainerTerminal*>(user); if (self->history_.empty()) return 0;
    const auto& line = self->history_.back(); std::fill(cells, cells + columns, VTermScreenCell{}); std::copy_n(line.begin(), std::min(columns, static_cast<int>(line.size())), cells);
    self->historyBytes_ -= line.size() * static_cast<qint64>(sizeof(VTermScreenCell)); self->history_.pop_back(); --self->scrolledLines_; return 1;
}
int ContainerTerminal::clear(void* user) { auto* self = static_cast<ContainerTerminal*>(user); self->scrolledLines_ -= static_cast<int>(self->history_.size()); self->history_.clear(); self->historyBytes_ = 0; return 1; }
void ContainerTerminal::output(const char* bytes, size_t length, void* user) {
    auto* self = static_cast<ContainerTerminal*>(user);
    if (self->outputBatch_) self->outputBatch_->append(bytes, static_cast<qsizetype>(length));
    else self->send(0, QByteArray(bytes, static_cast<qsizetype>(length)));
}

TerminalSurface::TerminalSurface(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true); setFlag(ItemAcceptsInputMethod, true); setActiveFocusOnTab(true); setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton); setClip(true);
    font_.setFamily("monospace"); font_.setStyleHint(QFont::Monospace); font_.setPixelSize(14); sizeScreen();
}
bool TerminalSurface::setTerminal(ContainerTerminal* terminal) {
    if (terminal_ == terminal) return true;
    if (terminal_) disconnect(terminal_, nullptr, this, nullptr);
    terminal_ = terminal; scroll_ = 0; selectionStart_ = selectionEnd_ = -1;
    if (terminal_) {
        connect(terminal_, &ContainerTerminal::screenChanged, this, [this](int scrolledLines) {
            scroll_ = std::clamp(scroll_ ? scroll_ + scrolledLines : 0, 0, terminal_->historyLines());
            update(); emit visibleTextChanged();
        });
        connect(terminal_, &QObject::destroyed, this, [this] { update(); emit terminalChanged(); emit visibleTextChanged(); });
    }
    sizeScreen(); update(); emit terminalChanged(); emit visibleTextChanged(); return true;
}
bool TerminalSurface::setForeground(QColor color) { foreground_ = color; update(); emit colorsChanged(); return true; }
bool TerminalSurface::setBackground(QColor color) { background_ = color; update(); emit colorsChanged(); return true; }
bool TerminalSurface::setFontFamily(const QString& family) { font_.setFamily(family); sizeScreen(); update(); emit colorsChanged(); return true; }
bool TerminalSurface::sizeScreen() {
    const QFontMetricsF metrics(font_); cellWidth_ = std::max<qreal>(1, metrics.horizontalAdvance('M')); cellHeight_ = std::max<qreal>(1, metrics.height()); ascent_ = metrics.ascent();
    if (width() < 2 * cellWidth_ || height() < 2 * cellHeight_) return true;
    return !terminal_ || terminal_->resize(std::clamp(static_cast<int>(height() / cellHeight_), 2, 120), std::clamp(static_cast<int>(width() / cellWidth_), 2, 320));
}
void TerminalSurface::geometryChange(const QRectF& next, const QRectF& old) { QQuickPaintedItem::geometryChange(next, old); if (next.size() != old.size()) sizeScreen(); }
void TerminalSurface::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), background_); if (!terminal_) return;
    for (int row = 0; row < terminal_->rows(); ++row) for (int column = 0; column < terminal_->columns(); ++column) {
        const auto cell = terminal_->cell(row, column, scroll_);
        auto fg = VTERM_COLOR_IS_DEFAULT_FG(&cell.fg) ? foreground_ : terminal_->color(cell.fg);
        auto bg = VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) ? background_ : terminal_->color(cell.bg);
        if (cell.attrs.reverse) std::swap(fg, bg);
        const int index = row * terminal_->columns() + column;
        if (selectionStart_ >= 0 && index >= std::min(selectionStart_, selectionEnd_) && index <= std::max(selectionStart_, selectionEnd_)) std::swap(fg, bg);
        const QRectF rect(column * cellWidth_, row * cellHeight_, cellWidth_ * std::max(1, static_cast<int>(cell.width)), cellHeight_);
        if (bg != background_) painter->fillRect(rect, bg);
        if (cell.width == 0 || cell.attrs.conceal) continue;
        auto font = font_; font.setBold(cell.attrs.bold); font.setItalic(cell.attrs.italic); font.setUnderline(cell.attrs.underline); font.setStrikeOut(cell.attrs.strike);
        painter->setFont(font); painter->setPen(fg); painter->drawText(QPointF(rect.x(), rect.y() + ascent_), characters(cell));
    }
    if (hasActiveFocus() && !scroll_ && terminal_->cursorVisible()) {
        const auto cursor = terminal_->cursor(); painter->setPen(foreground_); painter->drawRect(QRectF(cursor.col * cellWidth_, cursor.row * cellHeight_, cellWidth_, cellHeight_).adjusted(0, 0, -1, -1));
    }
}
void TerminalSurface::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::ShiftModifier) { emit controlsFocusRequested(); event->accept(); return; }
#ifdef Q_OS_MACOS
    const bool copy = event->matches(QKeySequence::Copy), paste = event->matches(QKeySequence::Paste);
#else
    const bool copy = event->key() == Qt::Key_C && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);
    const bool paste = event->key() == Qt::Key_V && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
    if (copy) { copySelection(); event->accept(); return; }
    if (paste) { if (terminal_ && terminal_->connected()) emit pasteRequested(); event->accept(); return; }
    if (event->modifiers() & guiModifier) { event->ignore(); return; }
    if (event->modifiers() & Qt::ShiftModifier && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) {
        if (terminal_) scroll_ = std::clamp(scroll_ + (event->key() == Qt::Key_PageUp ? terminal_->rows() : -terminal_->rows()), 0, terminal_->historyLines());
        update(); emit visibleTextChanged(); event->accept(); return;
    }
    if (terminal_ && terminal_->key(event->key(), event->text(), event->modifiers())) { followOutput(); event->accept(); } else event->ignore();
}
bool TerminalSurface::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride && hasActiveFocus() && terminal_ && terminal_->connected()) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!(key->modifiers() & guiModifier) || key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Paste)) { event->accept(); return true; }
    }
    return QQuickPaintedItem::event(event);
}
void TerminalSurface::inputMethodEvent(QInputMethodEvent* event) { if (terminal_ && terminal_->paste(event->commitString())) { followOutput(); event->accept(); } }
QVariant TerminalSurface::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) return terminal_ && terminal_->connected();
    if (query == Qt::ImCursorRectangle && terminal_) { const auto pos = terminal_->cursor(); return QRectF(pos.col * cellWidth_, pos.row * cellHeight_, cellWidth_, cellHeight_); }
    if (query == Qt::ImHints) return static_cast<int>(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    return QQuickPaintedItem::inputMethodQuery(query);
}
int TerminalSurface::position(const QPointF& point) const {
    if (!terminal_) return -1;
    return std::clamp(static_cast<int>(point.y() / cellHeight_), 0, terminal_->rows() - 1) * terminal_->columns() + std::clamp(static_cast<int>(point.x() / cellWidth_), 0, terminal_->columns() - 1);
}
void TerminalSurface::mousePressEvent(QMouseEvent* event) {
    forceActiveFocus(); if (!terminal_) return;
    const auto index = position(event->position());
    if (terminal_->mouseTracking() && !(event->modifiers() & Qt::ShiftModifier)) terminal_->mouse(index / terminal_->columns(), index % terminal_->columns(), event->button() == Qt::LeftButton ? 1 : 2, true, event->modifiers());
    else if (event->button() == Qt::LeftButton) { selecting_ = true; selectionStart_ = selectionEnd_ = index; update(); }
    event->accept();
}
void TerminalSurface::mouseMoveEvent(QMouseEvent* event) {
    if (!terminal_) return;
    const auto index = position(event->position());
    if (selecting_) { selectionEnd_ = index; update(); }
    else if (!scroll_) terminal_->mouse(index / terminal_->columns(), index % terminal_->columns(), 0, false, event->modifiers());
    event->accept();
}
void TerminalSurface::mouseReleaseEvent(QMouseEvent* event) {
    if (!terminal_) return;
    const auto index = position(event->position());
    if (selecting_) { selectionEnd_ = index; selecting_ = false; update(); }
    else terminal_->mouse(index / terminal_->columns(), index % terminal_->columns(), event->button() == Qt::LeftButton ? 1 : 2, false, event->modifiers());
    event->accept();
}
void TerminalSurface::wheelEvent(QWheelEvent* event) {
    if (!terminal_) return;
    const int delta = event->angleDelta().y() ? event->angleDelta().y() / 40 : event->pixelDelta().y() / 4;
    if (terminal_->mouseTracking() && !scroll_ && !(event->modifiers() & Qt::ShiftModifier)) {
        const auto index = position(event->position()); terminal_->mouse(index / terminal_->columns(), index % terminal_->columns(), delta > 0 ? 4 : 5, true, event->modifiers());
    } else { scroll_ = std::clamp(scroll_ + delta, 0, terminal_->historyLines()); selectionStart_ = selectionEnd_ = -1; update(); emit visibleTextChanged(); }
    event->accept();
}
void TerminalSurface::focusInEvent(QFocusEvent* event) { QQuickPaintedItem::focusInEvent(event); update(); }
bool TerminalSurface::followOutput() { scroll_ = 0; selectionStart_ = selectionEnd_ = -1; update(); emit visibleTextChanged(); return true; }
bool TerminalSurface::copySelection() {
    if (!terminal_ || selectionStart_ < 0 || !QGuiApplication::clipboard()) return false;
    QString result; const int first = std::min(selectionStart_, selectionEnd_), last = std::max(selectionStart_, selectionEnd_);
    for (int index = first; index <= last; ++index) {
        result += characters(terminal_->cell(index / terminal_->columns(), index % terminal_->columns(), scroll_));
        if (index < last && index % terminal_->columns() == terminal_->columns() - 1) result += '\n';
    }
    QGuiApplication::clipboard()->setText(result); return true;
}
QVariantMap TerminalSurface::clipboardForPaste() const {
    const auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return {{"text", QString{}}, {"error", "Clipboard is unavailable."}};
    const auto text = clipboard->text();
    if (text.isEmpty()) return {{"text", QString{}}, {"error", "Clipboard contains no text."}};
    if (text.size() > 65536 || text.toUtf8().size() > 65536)
        return {{"text", QString{}}, {"error", "Clipboard text exceeds 64 KiB. Nothing was truncated or sent."}};
    return {{"text", text}, {"error", QString{}}};
}
}
