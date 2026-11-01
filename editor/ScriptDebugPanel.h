#pragma once
#include <engine/ScriptDebug.h>

#include <QWidget>

class QLabel;
class QListWidget;
class QToolButton;
class QTreeWidget;

// Script debugger panel (bottom dock): where the game's script stopped, the call stack and the
// variables of the selected frame (locals, upvalues, the entity's props), and the buttons that
// let it go on: Continue (F8), Step over (F10), Step into (F11), Step out (Shift+F11), Pause.
class ScriptDebugPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScriptDebugPanel(QWidget* parent = nullptr);

    void showPause(const engine::ScriptDebugPause& pause);
    void showRunning(bool gameRunning); // no pause (running, or no game)
    bool paused() const { return paused_; }

signals:
    void command(const QString& word);                   // continue, next, step, out, pause
    void frameSelected(const QString& file, int line);   // breakpoint key and line

private:
    void showFrame(int index);
    void updateButtons();

    engine::ScriptDebugPause pause_;
    bool paused_ = false, running_ = false;
    QLabel* status_ = nullptr;
    QListWidget* stack_ = nullptr;
    QTreeWidget* vars_ = nullptr;
    QToolButton* continue_ = nullptr;
    QToolButton* over_ = nullptr;
    QToolButton* into_ = nullptr;
    QToolButton* out_ = nullptr;
    QToolButton* pause_btn_ = nullptr;
};
