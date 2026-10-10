#include "yaml_presentation.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTimer>

namespace podlord {
namespace {
class YamlHighlighter final : public QSyntaxHighlighter {
public:
    explicit YamlHighlighter(QObject* parent) : QSyntaxHighlighter(parent) {}
protected:
    void highlightBlock(const QString& text) override {
        const auto scalar = QColor("#d6c39a");
        int first = 0;
        while (first < text.size() && text[first].isSpace()) ++first;
        const int blockIndent = previousBlockState();
        setCurrentBlockState(-1);
        if (blockIndent >= 0 && (first == text.size() || first >= blockIndent)) {
            setCurrentBlockState(blockIndent); setFormat(0, text.size(), scalar); return;
        }
        int end = text.size(), colon = -1;
        QChar quote;
        for (int index = first; index < text.size(); ++index) {
            const auto character = text[index];
            if (quote == '"' && character == '\\') { ++index; continue; }
            if (quote == '\'' && character == '\'' && index+1 < text.size() && text[index+1] == '\'') { ++index; continue; }
            if (character == quote) { quote = {}; continue; }
            if (!quote.isNull()) continue;
            if (character == '"' || character == '\'') { quote = character; continue; }
            if (character == '#' && (index == 0 || text[index-1].isSpace())) { end = index; break; }
            if (colon < 0 && character == ':' && (index+1 == text.size() || text[index+1].isSpace())) colon = index;
        }
        if (end < text.size()) setFormat(end, text.size()-end, QColor("#6f7f8f"));
        if (first >= end) return;
        if (text[first] == '-' && (first+1 == end || text[first+1].isSpace())) {
            setFormat(first, 1, QColor("#d9a13b")); ++first;
            while (first < end && text[first].isSpace()) ++first;
        }
        QString key;
        if (colon > first && colon < end) {
            setFormat(first, colon-first, QColor("#6ec6ff"));
            key = text.mid(first, colon-first).trimmed();
            if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'') && key.back() == key.front()) key = key.mid(1,key.size()-2);
            first = colon+1;
            while (first < end && text[first].isSpace()) ++first;
        }
        if (first >= end) return;
        const auto value = text.mid(first,end-first).trimmed();
        static const QRegularExpression blockMarker("^[|>](?:[+-][1-9]?|[1-9][+-]?)?$");
        if (blockMarker.match(value).hasMatch()) {
            int indentation = 0;
            while (indentation < text.size() && text[indentation].isSpace()) ++indentation;
            setCurrentBlockState(indentation+1);
        }
        bool number = false;
        value.toDouble(&number);
        const bool keyword = value.compare("true",Qt::CaseInsensitive)==0 || value.compare("false",Qt::CaseInsensitive)==0
            || value.compare("null",Qt::CaseInsensitive)==0 || value == "~";
        const bool reference = key.compare("name",Qt::CaseInsensitive)==0 || key.compare("kind",Qt::CaseInsensitive)==0 || key.compare("namespace",Qt::CaseInsensitive)==0;
        QTextCharFormat format;
        format.setForeground(keyword ? QColor("#f0c44f") : number ? QColor("#7ddc8d") : reference ? QColor("#7ad8ff") : scalar);
        format.setFontUnderline(reference);
        setFormat(first,end-first,format);
    }
};
}
YamlPresentation::YamlPresentation(QQuickItem* parent) : QQuickPaintedItem(parent), highlighter_(new YamlHighlighter(this)) {
    connect(this,&QQuickItem::heightChanged,this,[this] { scheduleLines(); });
    connect(this,&QQuickItem::visibleChanged,this,[this] { scheduleLines(); });
}
void YamlPresentation::setDocument(QQuickTextDocument* document) {
    if (document_ == document) return;
    disconnect(contentConnection_); disconnect(documentConnection_);
    document_ = document;
    highlighter_->setDocument(document ? document->textDocument() : nullptr);
    if (document) {
        documentConnection_ = connect(document,&QQuickTextDocument::textDocumentChanged,this,[this] {
            const auto retained = document_;
            setDocument(nullptr); setDocument(retained);
        });
        contentConnection_ = connect(document->textDocument(),&QTextDocument::contentsChanged,this,[this] { scheduleLines(); });
    }
    emit documentChanged(); scheduleLines();
}
void YamlPresentation::setEditor(QQuickItem* editor) {
    if (editor_ == editor) return;
    editor_ = editor; emit editorChanged(); scheduleLines();
}
void YamlPresentation::setFont(const QFont& font) {
    if (font_ == font) return;
    font_ = font; emit fontChanged(); scheduleLines();
}
void YamlPresentation::setColor(const QColor& color) {
    if (color_ == color) return;
    color_ = color; emit colorChanged(); update();
}
void YamlPresentation::setScrollOffset(qreal offset) {
    if (offset_ == offset) return;
    offset_ = offset; emit scrollOffsetChanged(); scheduleLines();
}
bool YamlPresentation::scheduleLines() {
    if (queued_ || !isVisible()) return false;
    queued_ = true;
    QTimer::singleShot(0,this,[this] { queued_ = false; rebuildLines(); });
    return true;
}
bool YamlPresentation::rebuildLines() {
    lines_.clear();
    if (!document_ || !editor_ || !isVisible()) { update(); return false; }
    auto* document = document_->textDocument();
    setImplicitWidth(QFontMetricsF(font_).horizontalAdvance(QString::number(document->blockCount()))+16);
    int position = 0;
    if (!QMetaObject::invokeMethod(editor_,"positionAt",Qt::DirectConnection,Q_RETURN_ARG(int,position),
        Q_ARG(qreal,0),Q_ARG(qreal,qMax(qreal(0),offset_-editor_->y())))) return false;
    auto block = document->findBlock(position);
    firstLine_ = block.isValid() ? block.blockNumber()+1 : 1;
    for (; block.isValid(); block=block.next()) {
        QRectF rectangle;
        if (!QMetaObject::invokeMethod(editor_,"positionToRectangle",Qt::DirectConnection,Q_RETURN_ARG(QRectF,rectangle),Q_ARG(int,block.position()))) return false;
        rectangle.translate(0,editor_->y()-offset_);
        if (rectangle.top() > height()) break;
        if (rectangle.bottom() >= 0) lines_.append({block.blockNumber()+1,rectangle});
    }
    emit linesChanged(); update(); return true;
}
void YamlPresentation::paint(QPainter* painter) {
    painter->setFont(font_); painter->setPen(color_);
    for (const auto& line : lines_)
        painter->drawText(QRectF(0,line.rectangle.y(),width()-8,line.rectangle.height()),Qt::AlignRight|Qt::AlignVCenter,QString::number(line.number));
}
}
