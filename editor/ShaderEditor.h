#pragma once
#include <QSyntaxHighlighter>
#include <QWidget>
#include <map>

class CodeEditor;
class QLabel;
class QListWidget;
class QPlainTextEdit;

// GLSL colouring: keywords, types, built-ins, preprocessor, numbers, comments.
class GlslHighlighter : public QSyntaxHighlighter {
public:
    explicit GlslHighlighter(QTextDocument* doc);

protected:
    void highlightBlock(const QString& text) override;

private:
    QTextCharFormat keyword_, type_, builtin_, preprocessor_, number_, comment_, uniform_;
};

// Editor for the renderer's shaders (engine/shaders): pick a file, edit it and
// press Apply to save and recompile live. Compiler errors are marked in the
// text; on error the previous shaders keep running.
class ShaderEditor : public QWidget {
    Q_OBJECT
public:
    explicit ShaderEditor(const QString& directory, QWidget* parent = nullptr);

    bool isModified() const;
    bool saveAll(QString* error = nullptr);
    QString directory() const { return directory_; }
    void showResult(bool ok, const QString& log); // after the renderer recompiled

signals:
    void applyRequested(); // files are saved; recompile from directory()
    void modificationChanged(bool modified);

private:
    void openFile(const QString& name);
    void storeCurrent();
    void revertToBuiltin();
    void updateList();

    QString directory_;
    QListWidget* list_;
    CodeEditor* editor_;
    QPlainTextEdit* log_;
    QLabel* status_;
    QString current_;
    std::map<QString, QString> text_;  // edited text per file
    std::map<QString, QString> saved_; // text on disk (or built-in)
    bool loading_ = false;
};
