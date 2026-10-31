#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QStandardItemModel;

// Lua syntax colouring: keywords, engine API, strings, numbers, comments,
// including multi-line [[ ]] strings and --[[ ]] comments.
class LuaHighlighter : public QSyntaxHighlighter {
public:
    explicit LuaHighlighter(QTextDocument* doc);

protected:
    void highlightBlock(const QString& text) override;

private:
    QTextCharFormat keyword_, builtin_, api_, string_, number_, comment_, function_, method_;
};

// Text area for code: line numbers, current-line and bracket highlight,
// error marker, auto-indent, Tab = 4 spaces, Ctrl+/ comment, completion.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setErrorLine(int line, const QString& message); // 0 = no error
    void goToLine(int line);
    void toggleComment();
    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent* e);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override; // tooltips over the error line

private:
    void updateExtraSelections();
    void updateLineNumberArea(const QRect& rect, int dy);
    QString wordBeforeCursor() const;
    void showCompletion(bool force);
    void insertCompletion(const QString& text);

    QWidget* lineNumbers_;
    QCompleter* completer_;
    QStandardItemModel* words_;
    int errorLine_ = 0;
    QString errorMessage_;
};

// One open .lua file: editor, find/replace bar and live syntax check.
class ScriptEditor : public QWidget {
    Q_OBJECT
public:
    explicit ScriptEditor(QWidget* parent = nullptr);

    bool load(const QString& path, QString* error = nullptr);
    bool save(QString* error = nullptr);
    QString path() const { return path_; }
    bool isModified() const;
    QString title() const;      // file name, with * when modified
    CodeEditor* editor() const { return editor_; }

    void showFind(bool replace);
    void goToLinePrompt();

signals:
    void modificationChanged(bool modified);
    void saved(const QString& path);

private:
    void check();
    void find(bool backward);
    void replaceOne();
    void replaceAll();

    QString path_;
    CodeEditor* editor_;
    QWidget* findBar_;
    QLineEdit* findEdit_;
    QLineEdit* replaceEdit_;
    QWidget* replaceRow_;
    QCheckBox* caseBox_;
    QLabel* status_;
    QTimer checkTimer_;
};

// Template for File > New script: properties + callbacks, ready to attach to an entity.
QString newScriptTemplate();
