#include "ScriptEditor.h"

#include <engine/Scripting.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSet>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace {

struct ApiEntry {
    const char* name;
    const char* doc;
};

// Engine API offered by completion (keep in sync with assets/scripts/API.md).
const ApiEntry kApi[] = {
    {"function on_start(self)", "Called once when the level starts"},
    {"function on_update(self, dt)", "Called every fixed step (60 Hz), dt in seconds"},
    {"function on_damage(self, amount)", "Called when a weapon hits the entity"},
    {"function on_death(self)", "Called once when health reaches 0"},
    {"properties", "Tunables shown in the editor: { {\"name\", default, \"description\"}, ... }"},
    {"props", "Values of the declared properties for this entity (editor overrides applied)"},
    {"vec3", "vec3(x, y, z): vector with + - * , :length() :normalized() :flat() :dot(v)"},
    {"self.name", "Entity name (read only)"},
    {"self.type", "Entity type from the editor (read only)"},
    {"self.position", "Feet position (vec3); assigning teleports"},
    {"self.yaw", "Facing in degrees (0 = +X, 90 = +Z)"},
    {"self.color", "Box color (vec3 0..1)"},
    {"self.height", "Entity height in metres"},
    {"self.health", "Hit points, read/write"},
    {"self.alive", "false after death"},
    {"self.grounded", "Standing on the ground (characters)"},
    {"self.has_model", "Has an animated character model"},
    {"self.animation", "Name of the clip playing now"},
    {"self.animation_time", "Seconds into the current clip"},
    {"self.flash", "Seconds of white hit flash"},
    {"self:eye()", "Eye position (vec3)"},
    {"self:forward()", "Facing direction (vec3)"},
    {"self:enable_character()", "Gives the entity a capsule that walks with collision"},
    {"self:move(direction, speed)", "Walk this step (characters)"},
    {"self:jump(speed)", "Jump if grounded"},
    {"self:face(point)", "Turn toward a point"},
    {"self:log(text)", "Print with the entity name"},
    {"self:play(clip, { loop = true, speed = 1, fade = 0.2, restart = false })", "Play an animation clip"},
    {"self:animation_done()", "A non-looping clip reached its end"},
    {"self:has_animation(clip)", "The model has this clip"},
    {"game.player_position()", "Player feet position (vec3)"},
    {"game.player_eye()", "Player eye position (vec3)"},
    {"game.player_health()", "Player health"},
    {"game.damage_player(amount)", "Hurt the player"},
    {"game.time()", "Seconds since the level started"},
    {"game.can_see(from, to)", "true if no level geometry is between the points"},
    {"game.raycast(origin, dir, max_distance)", "hit, point, distance"},
    {"game.find(name)", "Another entity by name, or nil"},
    {"game.log(text)", "Print to the game console"},
};

const char* kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
                           "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
const char* kBuiltins[] = {"print", "pairs", "ipairs", "next", "select", "type", "tostring", "tonumber", "error",
                           "assert", "pcall", "xpcall", "setmetatable", "getmetatable", "rawget", "rawset",
                           "math", "string", "table", "coroutine",
                           "math.abs", "math.floor", "math.ceil", "math.min", "math.max", "math.sqrt", "math.sin",
                           "math.cos", "math.atan", "math.rad", "math.deg", "math.random", "math.pi", "math.huge",
                           "math.fmod", "math.exp", "math.log", "string.format", "string.sub", "string.find",
                           "string.len", "string.lower", "string.upper", "string.rep", "table.insert",
                           "table.remove", "table.concat", "table.sort", "table.unpack"};
const char* kApiRoots[] = {"game", "vec3", "self", "props", "properties"};

QString insertText(const QString& entry)
{
    // "self:move(direction, speed)" -> "self:move(" ; "function on_start(self)" stays whole.
    if (entry.startsWith("function "))
        return entry + "\n    \nend";
    const int paren = entry.indexOf('(');
    return paren >= 0 ? entry.left(paren + 1) : entry;
}

} // namespace

// ---------------------------------------------------------------- highlighter

LuaHighlighter::LuaHighlighter(QTextDocument* doc)
    : QSyntaxHighlighter(doc)
{
    keyword_.setForeground(QColor(0x00, 0x33, 0xb3));
    keyword_.setFontWeight(QFont::Bold);
    builtin_.setForeground(QColor(0x87, 0x10, 0x94));
    api_.setForeground(QColor(0x00, 0x7a, 0x7a));
    api_.setFontWeight(QFont::DemiBold);
    string_.setForeground(QColor(0x06, 0x7d, 0x17));
    number_.setForeground(QColor(0x17, 0x50, 0xeb));
    comment_.setForeground(QColor(0x8c, 0x8c, 0x8c));
    comment_.setFontItalic(true);
    function_.setForeground(QColor(0x00, 0x62, 0x7a));
    function_.setFontWeight(QFont::Bold);
    method_.setForeground(QColor(0x7a, 0x3e, 0x9d));
}

void LuaHighlighter::highlightBlock(const QString& text)
{
    static const QSet<QString> keywords = [] {
        QSet<QString> s;
        for (const char* k : kKeywords)
            s.insert(k);
        return s;
    }();
    static const QSet<QString> builtins = [] {
        QSet<QString> s;
        for (const char* b : kBuiltins)
            if (!QString(b).contains('.'))
                s.insert(b);
        return s;
    }();
    static const QSet<QString> api = [] {
        QSet<QString> s;
        for (const char* a : kApiRoots)
            s.insert(a);
        for (const char* cb : {"on_start", "on_update", "on_damage", "on_death"})
            s.insert(cb);
        return s;
    }();

    // Block state: -1 = normal; otherwise level * 2 + (1 = comment, 0 = string) inside a long bracket.
    int i = 0;
    const int n = static_cast<int>(text.size());
    auto closeLong = [&](int from, int level, const QTextCharFormat& fmt, bool comment) {
        const QString closer = "]" + QString(level, '=') + "]";
        const int end = static_cast<int>(text.indexOf(closer, from));
        if (end < 0) {
            setFormat(from, n - from, fmt);
            setCurrentBlockState(level * 2 + (comment ? 1 : 0));
            return n;
        }
        setFormat(from, end + static_cast<int>(closer.size()) - from, fmt);
        return end + static_cast<int>(closer.size());
    };
    auto longLevel = [&](int pos) -> int { // "[==[" at pos -> 2, -1 if not a long bracket
        if (pos >= n || text[pos] != '[')
            return -1;
        int p = pos + 1, level = 0;
        while (p < n && text[p] == '=')
            ++p, ++level;
        return p < n && text[p] == '[' ? level : -1;
    };

    setCurrentBlockState(-1);
    if (const int prev = previousBlockState(); prev >= 0)
        i = closeLong(0, prev / 2, (prev & 1) ? comment_ : string_, prev & 1);

    bool afterFunction = false;
    while (i < n) {
        const QChar c = text[i];
        if (c == '-' && i + 1 < n && text[i + 1] == '-') {
            if (const int level = longLevel(i + 2); level >= 0) {
                i = closeLong(i, level, comment_, true);
                continue;
            }
            setFormat(i, n - i, comment_);
            break;
        }
        if (c == '[') {
            if (const int level = longLevel(i); level >= 0) {
                i = closeLong(i, level, string_, false);
                continue;
            }
        }
        if (c == '"' || c == '\'') {
            int j = i + 1;
            while (j < n && text[j] != c) {
                if (text[j] == '\\')
                    ++j;
                ++j;
            }
            j = std::min(j + 1, n);
            setFormat(i, j - i, string_);
            i = j;
            continue;
        }
        if (c.isDigit() || (c == '.' && i + 1 < n && text[i + 1].isDigit())) {
            static const QRegularExpression num(R"(0[xX][0-9a-fA-F]+|\d*\.?\d+(?:[eE][+-]?\d+)?)");
            const auto m = num.match(text, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            const int len = m.hasMatch() ? static_cast<int>(m.capturedLength()) : 1;
            setFormat(i, len, number_);
            i += len;
            continue;
        }
        if (c.isLetter() || c == '_') {
            int j = i;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == '_'))
                ++j;
            const QString word = text.mid(i, j - i);
            const bool isMethod = i > 0 && (text[i - 1] == ':' || text[i - 1] == '.');
            if (keywords.contains(word)) {
                setFormat(i, j - i, keyword_);
                afterFunction = word == "function";
                i = j;
                continue;
            }
            if (afterFunction)
                setFormat(i, j - i, function_);
            else if (isMethod)
                setFormat(i, j - i, method_);
            else if (api.contains(word))
                setFormat(i, j - i, api_);
            else if (builtins.contains(word))
                setFormat(i, j - i, builtin_);
            afterFunction = false;
            i = j;
            continue;
        }
        if (!c.isSpace() && c != '.' && c != ':')
            afterFunction = false;
        ++i;
    }
}

// ---------------------------------------------------------------- code editor

namespace {
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor)
        : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return {editor_->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* e) override { editor_->paintLineNumbers(e); }

private:
    CodeEditor* editor_;
};
} // namespace

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(std::max(10, mono.pointSize()));
    setFont(mono);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    new LuaHighlighter(document());

    lineNumbers_ = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateExtraSelections);
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);

    words_ = new QStandardItemModel(this);
    completer_ = new QCompleter(words_, this);
    completer_->setWidget(this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setFilterMode(Qt::MatchStartsWith);
    completer_->setMaxVisibleItems(12);
    connect(completer_, qOverload<const QString&>(&QCompleter::activated), this, &CodeEditor::insertCompletion);
    updateExtraSelections();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int max = std::max(1, blockCount()); max >= 10; max /= 10)
        ++digits;
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * std::max(3, digits);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        lineNumbers_->scroll(0, dy);
    else
        lineNumbers_->update(0, rect.y(), lineNumbers_->width(), rect.height());
}

void CodeEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    lineNumbers_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::paintLineNumbers(QPaintEvent* e)
{
    QPainter painter(lineNumbers_);
    painter.fillRect(e->rect(), QColor(0xf3, 0xf3, 0xf3));
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber() + 1;
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber() + 1;
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            if (number == errorLine_)
                painter.fillRect(0, top, lineNumbers_->width(), bottom - top, QColor(0xff, 0xd6, 0xd6));
            painter.setPen(number == current ? QColor(0x20, 0x20, 0x20) : QColor(0x99, 0x99, 0x99));
            painter.drawText(0, top, lineNumbers_->width() - 6, fontMetrics().height(), Qt::AlignRight,
                             QString::number(number));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::setErrorLine(int line, const QString& message)
{
    errorLine_ = line;
    errorMessage_ = message;
    updateExtraSelections();
    lineNumbers_->update();
}

void CodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(std::max(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor c(block);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

void CodeEditor::updateExtraSelections()
{
    QList<QTextEdit::ExtraSelection> list;
    QTextEdit::ExtraSelection line;
    line.format.setBackground(QColor(0xff, 0xfb, 0xe0));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    list.append(line);

    if (errorLine_ > 0) {
        const QTextBlock block = document()->findBlockByNumber(errorLine_ - 1);
        if (block.isValid()) {
            QTextEdit::ExtraSelection err;
            err.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            err.format.setUnderlineColor(Qt::red);
            err.cursor = QTextCursor(block);
            err.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            list.append(err);
        }
    }

    // Matching bracket next to the cursor.
    const QString text = toPlainText();
    const int pos = textCursor().position();
    auto match = [&](int at) {
        if (at < 0 || at >= text.size())
            return false;
        const QString open = "([{", close = ")]}";
        const QChar c = text[at];
        int dir = 0;
        QChar want;
        if (const int k = static_cast<int>(open.indexOf(c)); k >= 0) {
            dir = 1;
            want = close[k];
        } else if (const int k2 = static_cast<int>(close.indexOf(c)); k2 >= 0) {
            dir = -1;
            want = open[k2];
        } else {
            return false;
        }
        int depth = 0;
        for (int p = at; p >= 0 && p < text.size(); p += dir) {
            if (text[p] == c)
                ++depth;
            else if (text[p] == want && --depth == 0) {
                for (int q : {at, p}) {
                    QTextEdit::ExtraSelection b;
                    b.format.setBackground(QColor(0xc8, 0xe6, 0xc9));
                    b.cursor = textCursor();
                    b.cursor.setPosition(q);
                    b.cursor.setPosition(q + 1, QTextCursor::KeepAnchor);
                    list.append(b);
                }
                return true;
            }
        }
        return false;
    };
    if (!match(pos - 1))
        match(pos);
    setExtraSelections(list);
}

bool CodeEditor::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip && errorLine_ > 0) {
        auto* help = static_cast<QHelpEvent*>(e);
        const QTextCursor c = cursorForPosition(viewport()->mapFrom(this, help->pos()));
        if (c.blockNumber() + 1 == errorLine_) {
            QToolTip::showText(help->globalPos(), errorMessage_, this);
            return true;
        }
        QToolTip::hideText();
    }
    return QPlainTextEdit::event(e);
}

void CodeEditor::toggleComment()
{
    QTextCursor c = textCursor();
    const int startBlock = document()->findBlock(c.selectionStart()).blockNumber();
    int endBlock = document()->findBlock(c.selectionEnd()).blockNumber();
    if (c.hasSelection() && document()->findBlock(c.selectionEnd()).position() == c.selectionEnd() && endBlock > startBlock)
        --endBlock; // selection ends at the start of a line: leave that line alone
    bool allCommented = true;
    for (int b = startBlock; b <= endBlock; ++b) {
        const QString t = document()->findBlockByNumber(b).text().trimmed();
        if (!t.isEmpty() && !t.startsWith("--"))
            allCommented = false;
    }
    c.beginEditBlock();
    for (int b = startBlock; b <= endBlock; ++b) {
        const QTextBlock block = document()->findBlockByNumber(b);
        const QString t = block.text();
        const int indent = static_cast<int>(t.size() - QString(t).remove(QRegularExpression("^\\s+")).size());
        QTextCursor lc(block);
        lc.setPosition(block.position() + indent);
        if (allCommented) {
            if (t.mid(indent, 3) == "-- ")
                lc.setPosition(lc.position() + 3, QTextCursor::KeepAnchor);
            else if (t.mid(indent, 2) == "--")
                lc.setPosition(lc.position() + 2, QTextCursor::KeepAnchor);
            lc.removeSelectedText();
        } else if (!t.trimmed().isEmpty()) {
            lc.insertText("-- ");
        }
    }
    c.endEditBlock();
}

QString CodeEditor::wordBeforeCursor() const
{
    const QTextCursor c = textCursor();
    const QString line = c.block().text().left(c.positionInBlock());
    int i = static_cast<int>(line.size());
    while (i > 0 && (line[i - 1].isLetterOrNumber() || line[i - 1] == '_' || line[i - 1] == '.' || line[i - 1] == ':'))
        --i;
    return line.mid(i);
}

void CodeEditor::showCompletion(bool force)
{
    const QString prefix = wordBeforeCursor();
    if (!force && (prefix.size() < 2 && !prefix.endsWith('.') && !prefix.endsWith(':'))) {
        completer_->popup()->hide();
        return;
    }
    // API entries + keywords + identifiers already used in this file.
    words_->clear();
    QSet<QString> seen;
    auto add = [&](const QString& text, const QString& doc) {
        if (text.isEmpty() || seen.contains(text))
            return;
        seen.insert(text);
        auto* item = new QStandardItem(text);
        if (!doc.isEmpty())
            item->setToolTip(doc);
        words_->appendRow(item);
    };
    for (const ApiEntry& e : kApi) {
        add(QString::fromLatin1(e.name), QString::fromLatin1(e.doc));
        // `self.x` entries also complete when the entity variable is called something else (e.g. `guard:`).
    }
    for (const char* k : kKeywords)
        add(k, QString());
    for (const char* b : kBuiltins)
        add(b, QString());
    static const QRegularExpression ident(R"([A-Za-z_][A-Za-z0-9_]{2,})");
    auto it = ident.globalMatch(toPlainText());
    while (it.hasNext())
        add(it.next().captured(), QString());

    completer_->setCompletionPrefix(prefix);
    if (completer_->completionCount() == 0 || (completer_->completionCount() == 1 && completer_->currentCompletion() == prefix)) {
        completer_->popup()->hide();
        return;
    }
    completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
    QRect r = cursorRect();
    r.translate(viewportMargins().left(), 0);
    r.setWidth(std::max(320, completer_->popup()->sizeHintForColumn(0) + completer_->popup()->verticalScrollBar()->sizeHint().width()));
    completer_->complete(r);
}

void CodeEditor::insertCompletion(const QString& text)
{
    QTextCursor c = textCursor();
    const QString prefix = wordBeforeCursor();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, static_cast<int>(prefix.size()));
    // Keep the indentation of the line for multi-line snippets.
    const QString line = c.block().text();
    const QString indent = line.left(static_cast<int>(line.size() - QString(line).remove(QRegularExpression("^\\s+")).size()));
    QString insert = insertText(text);
    insert.replace("\n", "\n" + indent);
    c.insertText(insert);
    if (insert.contains("\n    \n")) { // function snippet: put the cursor in the body
        c.movePosition(QTextCursor::Up);
        c.movePosition(QTextCursor::EndOfBlock);
    }
    setTextCursor(c);
}

void CodeEditor::keyPressEvent(QKeyEvent* e)
{
    if (completer_->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab:
        case Qt::Key_Escape:
        case Qt::Key_Backtab:
            e->ignore(); // the completer handles these
            return;
        default:
            break;
        }
    }
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (ctrl && e->key() == Qt::Key_Space) {
        showCompletion(true);
        return;
    }
    if (ctrl && (e->key() == Qt::Key_Slash || e->text() == "/")) {
        toggleComment();
        return;
    }
    if (e->key() == Qt::Key_Tab && !textCursor().hasSelection()) {
        insertPlainText("    ");
        return;
    }
    if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
        // Indent / unindent the selected lines.
        QTextCursor c = textCursor();
        const int first = document()->findBlock(c.selectionStart()).blockNumber();
        const int last = document()->findBlock(c.selectionEnd() - (c.hasSelection() ? 1 : 0)).blockNumber();
        c.beginEditBlock();
        for (int b = first; b <= last; ++b) {
            QTextCursor lc(document()->findBlockByNumber(b));
            if (e->key() == Qt::Key_Tab) {
                lc.insertText("    ");
            } else {
                const QString t = lc.block().text();
                int k = 0;
                while (k < 4 && k < t.size() && t[k] == ' ')
                    ++k;
                lc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, k);
                lc.removeSelectedText();
            }
        }
        c.endEditBlock();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        // Keep the indentation; indent one more level after a block opener.
        const QString line = textCursor().block().text().left(textCursor().positionInBlock());
        QString indent = line.left(static_cast<int>(line.size() - QString(line).remove(QRegularExpression("^\\s+")).size()));
        static const QRegularExpression opener(R"((\bthen|\bdo|\belse|\brepeat|\bfunction\b.*\)|[{(])\s*(--.*)?$)");
        if (opener.match(line).hasMatch() && !line.trimmed().startsWith("--"))
            indent += "    ";
        QPlainTextEdit::keyPressEvent(e);
        insertPlainText(indent);
        return;
    }
    QPlainTextEdit::keyPressEvent(e);

    // Dedent "end", "else", "elseif", "until", "}" when typed at the start of a line.
    if (!e->text().isEmpty() && e->text()[0].isLetter()) {
        QTextCursor c = textCursor();
        const QString t = c.block().text();
        const QString trimmed = t.trimmed();
        static const QSet<QString> closers{"end", "else", "elseif", "until"};
        if (closers.contains(trimmed) && c.positionInBlock() == t.size() && t.startsWith("    ")) {
            // Match the indentation of the previous non-empty line minus one level.
            QTextBlock prev = c.block().previous();
            while (prev.isValid() && prev.text().trimmed().isEmpty())
                prev = prev.previous();
            const QString pt = prev.isValid() ? prev.text() : QString();
            const int prevIndent = static_cast<int>(pt.size() - QString(pt).remove(QRegularExpression("^\\s+")).size());
            const int ownIndent = static_cast<int>(t.size() - trimmed.size());
            if (ownIndent >= prevIndent && ownIndent >= 4) {
                QTextCursor lc(c.block());
                lc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 4);
                lc.removeSelectedText();
            }
        }
    }

    const QString typed = e->text();
    if (!typed.isEmpty() && (typed[0].isLetterOrNumber() || typed == "_" || typed == "." || typed == ":"))
        showCompletion(false);
    else if (completer_->popup()->isVisible())
        completer_->popup()->hide();
}

// ---------------------------------------------------------------- script editor

ScriptEditor::ScriptEditor(QWidget* parent)
    : QWidget(parent)
{
    editor_ = new CodeEditor(this);

    findBar_ = new QWidget(this);
    auto* findLayout = new QVBoxLayout(findBar_);
    findLayout->setContentsMargins(4, 2, 4, 2);
    auto* findRow = new QHBoxLayout();
    findEdit_ = new QLineEdit(findBar_);
    findEdit_->setPlaceholderText(tr("Find"));
    findEdit_->setClearButtonEnabled(true);
    auto* prev = new QPushButton(tr("Previous"), findBar_);
    auto* next = new QPushButton(tr("Next"), findBar_);
    caseBox_ = new QCheckBox(tr("Match case"), findBar_);
    auto* close = new QToolButton(findBar_);
    close->setText(QStringLiteral("✕"));
    findRow->addWidget(findEdit_, 1);
    findRow->addWidget(prev);
    findRow->addWidget(next);
    findRow->addWidget(caseBox_);
    findRow->addWidget(close);
    replaceRow_ = new QWidget(findBar_);
    auto* replaceLayout = new QHBoxLayout(replaceRow_);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceEdit_ = new QLineEdit(replaceRow_);
    replaceEdit_->setPlaceholderText(tr("Replace with"));
    auto* replace = new QPushButton(tr("Replace"), replaceRow_);
    auto* replaceAllButton = new QPushButton(tr("Replace all"), replaceRow_);
    replaceLayout->addWidget(replaceEdit_, 1);
    replaceLayout->addWidget(replace);
    replaceLayout->addWidget(replaceAllButton);
    findLayout->addLayout(findRow);
    findLayout->addWidget(replaceRow_);
    findBar_->hide();

    status_ = new QLabel(this);
    status_->setContentsMargins(6, 3, 6, 3);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setCursor(Qt::PointingHandCursor);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(editor_, 1);
    layout->addWidget(findBar_);
    layout->addWidget(status_);

    connect(findEdit_, &QLineEdit::returnPressed, this, [this] { find(false); });
    connect(next, &QPushButton::clicked, this, [this] { find(false); });
    connect(prev, &QPushButton::clicked, this, [this] { find(true); });
    connect(close, &QToolButton::clicked, this, [this] {
        findBar_->hide();
        editor_->setFocus();
    });
    connect(replace, &QPushButton::clicked, this, &ScriptEditor::replaceOne);
    connect(replaceAllButton, &QPushButton::clicked, this, &ScriptEditor::replaceAll);

    checkTimer_.setSingleShot(true);
    checkTimer_.setInterval(350);
    connect(&checkTimer_, &QTimer::timeout, this, &ScriptEditor::check);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] { checkTimer_.start(); });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &ScriptEditor::modificationChanged);
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] { check(); });
}

bool ScriptEditor::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    editor_->setPlainText(QString::fromUtf8(f.readAll()));
    editor_->document()->setModified(false);
    check();
    return true;
}

bool ScriptEditor::save(QString* error)
{
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly) || f.write(editor_->toPlainText().toUtf8()) < 0 || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    editor_->document()->setModified(false);
    emit saved(path_);
    return true;
}

bool ScriptEditor::isModified() const
{
    return editor_->document()->isModified();
}

QString ScriptEditor::title() const
{
    return QFileInfo(path_).fileName() + (isModified() ? "*" : "");
}

void ScriptEditor::check()
{
    const QTextCursor c = editor_->textCursor();
    const QString where = tr("Line %1, column %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1);
    const engine::ScriptCheck result =
        engine::checkScriptSyntax(editor_->toPlainText().toStdString(), QFileInfo(path_).fileName().toStdString());
    if (result.ok) {
        editor_->setErrorLine(0, QString());
        status_->setStyleSheet("background: #e8f5e9; color: #1b5e20;");
        status_->setText(tr("✓ Lua syntax OK    ·    %1    ·    Ctrl+Space complete · Ctrl+/ comment · Ctrl+F find · Ctrl+G go to line").arg(where));
    } else {
        editor_->setErrorLine(result.line, QString::fromStdString(result.message));
        status_->setStyleSheet("background: #ffebee; color: #b71c1c;");
        status_->setText(tr("✗ Line %1: %2    ·    %3").arg(result.line).arg(QString::fromStdString(result.message), where));
    }
}

void ScriptEditor::showFind(bool replace)
{
    findBar_->show();
    replaceRow_->setVisible(replace);
    const QString selected = editor_->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        findEdit_->setText(selected);
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void ScriptEditor::find(bool backward)
{
    const QString text = findEdit_->text();
    if (text.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (caseBox_->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (!editor_->find(text, flags)) {
        // Wrap around.
        QTextCursor c = editor_->textCursor();
        c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        editor_->setTextCursor(c);
        if (!editor_->find(text, flags))
            findEdit_->setStyleSheet("background: #ffebee;");
        else
            findEdit_->setStyleSheet(QString());
    } else {
        findEdit_->setStyleSheet(QString());
    }
}

void ScriptEditor::replaceOne()
{
    QTextCursor c = editor_->textCursor();
    const Qt::CaseSensitivity cs = caseBox_->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (c.hasSelection() && QString::compare(c.selectedText(), findEdit_->text(), cs) == 0)
        c.insertText(replaceEdit_->text());
    find(false);
}

void ScriptEditor::replaceAll()
{
    const QString text = findEdit_->text();
    if (text.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (caseBox_->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    QTextCursor c(editor_->document());
    c.beginEditBlock();
    int count = 0;
    QTextCursor found = editor_->document()->find(text, 0, flags);
    while (!found.isNull()) {
        found.insertText(replaceEdit_->text());
        ++count;
        found = editor_->document()->find(text, found, flags);
    }
    c.endEditBlock();
    status_->setText(tr("Replaced %1 occurrence(s)").arg(count));
}

void ScriptEditor::goToLinePrompt()
{
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Go to line"), tr("Line:"), editor_->textCursor().blockNumber() + 1, 1,
                                          editor_->blockCount(), 1, &ok);
    if (ok)
        editor_->goToLine(line);
}

QString newScriptTemplate()
{
    return QStringLiteral(R"(-- New entity script. Attach it to an entity in the editor (Properties > Lua script).
-- API: assets/scripts/API.md (Ctrl+Space in the script editor lists it).

-- Tunables shown in the editor under Properties > Script parameters.
properties = {
    { "speed", 2.0, "Movement speed, m/s" },
}

function on_start(self)
    self:log("started")
end

function on_update(self, dt)
end

function on_damage(self, amount)
    self.flash = 0.12
end

function on_death(self)
end
)");
}
