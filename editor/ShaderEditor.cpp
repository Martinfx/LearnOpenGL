#include "ShaderEditor.h"
#include "ScriptEditor.h"

#include <engine/Renderer.h>

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

GlslHighlighter::GlslHighlighter(QTextDocument* doc)
    : QSyntaxHighlighter(doc)
{
    keyword_.setForeground(QColor(198, 120, 221));
    keyword_.setFontWeight(QFont::Bold);
    type_.setForeground(QColor(86, 182, 194));
    builtin_.setForeground(QColor(97, 175, 239));
    preprocessor_.setForeground(QColor(229, 192, 123));
    number_.setForeground(QColor(209, 154, 102));
    comment_.setForeground(QColor(110, 118, 129));
    comment_.setFontItalic(true);
    uniform_.setForeground(QColor(224, 108, 117));
}

void GlslHighlighter::highlightBlock(const QString& text)
{
    static const QRegularExpression keywords(
        "\\b(if|else|for|while|do|return|break|continue|discard|const|in|out|inout|uniform|layout|struct|"
        "binding|std140|location|flat|smooth|true|false|precision|highp|mediump|lowp)\\b");
    static const QRegularExpression types(
        "\\b(void|bool|int|uint|float|double|[biud]?vec[234]|mat[234](x[234])?|sampler2D|sampler2DArray|"
        "sampler2DArrayShadow|sampler2DShadow|samplerCube|sampler3D)\\b");
    static const QRegularExpression builtins(
        "\\b(texture|textureLod|textureSize|texelFetch|mix|clamp|smoothstep|step|dot|cross|normalize|length|"
        "distance|reflect|refract|pow|exp|exp2|log|log2|sqrt|inversesqrt|abs|sign|floor|ceil|fract|mod|min|max|"
        "sin|cos|tan|asin|acos|atan|radians|degrees|transpose|inverse|dFdx|dFdy|fwidth|gl_Position|gl_FragCoord|"
        "gl_VertexID|gl_FragDepth)\\b");
    static const QRegularExpression uniforms("\\bu[A-Z]\\w*");
    static const QRegularExpression numbers("\\b\\d+(\\.\\d*)?([eE][-+]?\\d+)?[fu]?\\b|\\.\\d+\\b");
    auto apply = [&](const QRegularExpression& re, const QTextCharFormat& fmt) {
        for (auto it = re.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            setFormat(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), fmt);
        }
    };
    apply(numbers, number_);
    apply(types, type_);
    apply(builtins, builtin_);
    apply(uniforms, uniform_);
    apply(keywords, keyword_);
    if (text.trimmed().startsWith('#'))
        setFormat(0, static_cast<int>(text.size()), preprocessor_);

    // Comments, including /* */ over several lines.
    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : static_cast<int>(text.indexOf("/*"));
    const int line = static_cast<int>(text.indexOf("//"));
    if (line >= 0 && (start < 0 || line < start) && previousBlockState() != 1)
        setFormat(line, static_cast<int>(text.size()) - line, comment_);
    while (start >= 0) {
        const int end = static_cast<int>(text.indexOf("*/", start));
        if (end < 0) {
            setCurrentBlockState(1);
            setFormat(start, static_cast<int>(text.size()) - start, comment_);
            break;
        }
        setFormat(start, end - start + 2, comment_);
        start = static_cast<int>(text.indexOf("/*", end + 2));
    }
}

ShaderEditor::ShaderEditor(const QString& directory, QWidget* parent)
    : QWidget(parent), directory_(directory)
{
    QDir().mkpath(directory_);
    // Every built-in shader, with the version on disk where there is one.
    for (const auto& [name, source] : engine::Renderer::builtinShaders()) {
        const QString file = QString::fromStdString(name);
        QFile disk(QDir(directory_).filePath(file));
        saved_[file] = disk.open(QIODevice::ReadOnly) ? QString::fromUtf8(disk.readAll()) : QString::fromStdString(source);
        text_[file] = saved_[file];
    }

    list_ = new QListWidget(this);
    list_->setMinimumWidth(150);
    editor_ = new CodeEditor(this);
    new GlslHighlighter(editor_->document());
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumHeight(110);
    log_->setPlaceholderText(tr("Compiler output"));
    status_ = new QLabel(this);

    auto* apply = new QPushButton(tr("Apply (Ctrl+S)"), this);
    apply->setToolTip(tr("Save the changed shaders and recompile them in the viewport"));
    auto* revert = new QPushButton(tr("Revert file to built-in"), this);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(apply);
    buttons->addWidget(revert);
    buttons->addWidget(status_, 1);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addLayout(buttons);
    rightLayout->addWidget(editor_, 1);
    rightLayout->addWidget(log_);
    auto* split = new QSplitter(this);
    split->addWidget(list_);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split);

    connect(list_, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        openFile(text.section(' ', 0, 0));
    });
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_)
            return;
        storeCurrent();
        updateList();
        emit modificationChanged(isModified());
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        QString error;
        if (!saveAll(&error)) {
            status_->setText(error);
            return;
        }
        emit applyRequested();
    });
    connect(revert, &QPushButton::clicked, this, &ShaderEditor::revertToBuiltin);
    auto* shortcut = new QShortcut(QKeySequence::Save, this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, apply, &QPushButton::click);

    updateList();
    list_->setCurrentRow(0);
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->text().startsWith("pbr.frag"))
            list_->setCurrentRow(i);
}

void ShaderEditor::updateList()
{
    const QString selected = current_;
    const QSignalBlocker block(list_);
    list_->clear();
    const auto& builtin = engine::Renderer::builtinShaders();
    for (const auto& [file, text] : text_) {
        QString label = file;
        if (text != saved_[file])
            label += " *";
        else if (text != QString::fromStdString(builtin.at(file.toStdString())))
            label += tr(" (custom)");
        list_->addItem(label);
        if (file == selected)
            list_->setCurrentRow(list_->count() - 1);
    }
}

void ShaderEditor::openFile(const QString& name)
{
    if (name.isEmpty() || name == current_ || !text_.count(name))
        return;
    current_ = name;
    loading_ = true;
    editor_->setPlainText(text_[name]);
    editor_->setErrorLine(0, {});
    loading_ = false;
}

void ShaderEditor::storeCurrent()
{
    if (!current_.isEmpty())
        text_[current_] = editor_->toPlainText();
}

bool ShaderEditor::isModified() const
{
    for (const auto& [file, text] : text_)
        if (text != saved_.at(file))
            return true;
    return false;
}

bool ShaderEditor::saveAll(QString* error)
{
    storeCurrent();
    for (auto& [file, text] : text_) {
        if (text == saved_[file])
            continue;
        QSaveFile out(QDir(directory_).filePath(file));
        if (!out.open(QIODevice::WriteOnly) || out.write(text.toUtf8()) < 0 || !out.commit()) {
            if (error)
                *error = tr("Cannot write %1").arg(out.fileName());
            return false;
        }
        saved_[file] = text;
    }
    updateList();
    emit modificationChanged(false);
    return true;
}

void ShaderEditor::revertToBuiltin()
{
    if (current_.isEmpty())
        return;
    const auto& builtin = engine::Renderer::builtinShaders();
    editor_->setPlainText(QString::fromStdString(builtin.at(current_.toStdString())));
}

void ShaderEditor::showResult(bool ok, const QString& log)
{
    status_->setText(ok ? tr("Compiled and applied.") : tr("Compile error: the previous shaders keep running."));
    status_->setStyleSheet(ok ? "color: #7fc97f;" : "color: #ff6b6b;");
    log_->setPlainText(ok ? QString() : log);
    editor_->setErrorLine(0, {});
    if (ok)
        return;
    // Messages look like "pbr.frag: 0:42(7): error: ..." (Mesa) or "pbr.frag: ERROR: 0:42: ..." (others);
    // source string 0 is the file itself, 1.. are its includes in order.
    static const QRegularExpression re("^(\\S+): .*?(\\d+):(\\d+)(?:\\(\\d+\\))?:\\s*(.*)$", QRegularExpression::MultilineOption);
    const auto m = re.match(log);
    if (!m.hasMatch())
        return;
    QString file = m.captured(1);
    const int sourceString = m.captured(2).toInt();
    if (sourceString > 0) {
        // Find the n-th #include of that file.
        static const QRegularExpression inc("#include\\s+\"([^\"]+)\"");
        int n = 0;
        for (auto it = inc.globalMatch(text_[file]); it.hasNext();) {
            const auto im = it.next();
            if (++n == sourceString)
                file = im.captured(1);
        }
    }
    if (!text_.count(file))
        return;
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->text().section(' ', 0, 0) == file)
            list_->setCurrentRow(i);
    editor_->setErrorLine(m.captured(3).toInt(), m.captured(4));
    editor_->goToLine(m.captured(3).toInt());
}
