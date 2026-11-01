#include "ScriptDebugPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

ScriptDebugPanel::ScriptDebugPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* bar = new QHBoxLayout();
    auto button = [&](const QString& text, const QString& tip, const QString& word) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, word] { emit command(word); });
        bar->addWidget(b);
        return b;
    };
    continue_ = button(tr("Continue"), tr("Let the script run on (F8)"), "continue");
    over_ = button(tr("Step over"), tr("Next line, calls run through (F10)"), "next");
    into_ = button(tr("Step into"), tr("Next line, into calls (F11)"), "step");
    out_ = button(tr("Step out"), tr("Run until the function returns (Shift+F11)"), "out");
    pause_btn_ = button(tr("Break"), tr("Stop at the next line any script runs"), "pause");
    bar->addSpacing(12);
    status_ = new QLabel(this);
    bar->addWidget(status_, 1);
    layout->addLayout(bar);

    auto* split = new QSplitter(Qt::Horizontal, this);
    stack_ = new QListWidget(split);
    stack_->setToolTip(tr("Call stack, innermost first: click a frame to see its variables and its line"));
    vars_ = new QTreeWidget(split);
    vars_->setColumnCount(3);
    vars_->setHeaderLabels({tr("Name"), tr("Value"), tr("Kind")});
    vars_->setRootIsDecorated(false);
    vars_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    vars_->setAlternatingRowColors(true);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    layout->addWidget(split, 1);

    connect(stack_, &QListWidget::currentRowChanged, this, [this](int row) {
        showFrame(row);
        if (row >= 0 && row < static_cast<int>(pause_.frames.size()))
            emit frameSelected(QString::fromStdString(pause_.frames[static_cast<std::size_t>(row)].file),
                               pause_.frames[static_cast<std::size_t>(row)].line);
    });
    showRunning(false);
}

void ScriptDebugPanel::showPause(const engine::ScriptDebugPause& pause)
{
    pause_ = pause;
    paused_ = true;
    running_ = true;
    const QString why = pause.reason == "breakpoint" ? tr("breakpoint") : pause.reason == "pause" ? tr("break") : tr("step");
    status_->setText(tr("<b>Paused</b> (%1) at %2:%3%4")
                         .arg(why, QString::fromStdString(pause.file))
                         .arg(pause.line)
                         .arg(pause.entity.empty() ? QString() : tr(" · entity <b>%1</b>").arg(QString::fromStdString(pause.entity).toHtmlEscaped())));
    stack_->blockSignals(true);
    stack_->clear();
    for (const engine::ScriptDebugFrame& f : pause.frames)
        stack_->addItem(QStringLiteral("%1   %2:%3").arg(QString::fromStdString(f.function), QString::fromStdString(f.file)).arg(f.line));
    stack_->setCurrentRow(0);
    stack_->blockSignals(false);
    showFrame(0);
    updateButtons();
}

void ScriptDebugPanel::showRunning(bool gameRunning)
{
    paused_ = false;
    running_ = gameRunning;
    status_->setText(gameRunning ? tr("Game running – breakpoints: click the line numbers of a script or press F9")
                                 : tr("Play the level (F5) to debug its scripts. Breakpoints: click the line numbers of a script or press F9."));
    stack_->clear();
    vars_->clear();
    updateButtons();
}

void ScriptDebugPanel::showFrame(int index)
{
    vars_->clear();
    if (index < 0 || index >= static_cast<int>(pause_.frames.size()))
        return;
    for (const engine::ScriptDebugVar& v : pause_.frames[static_cast<std::size_t>(index)].vars) {
        auto* item = new QTreeWidgetItem(vars_, {QString::fromStdString(v.name), QString::fromStdString(v.value),
                                                 QString::fromStdString(v.scope + " " + v.type)});
        item->setToolTip(1, QString::fromStdString(v.value));
        if (v.scope == "props")
            item->setForeground(0, QColor(0x2a, 0x6e, 0xb0));
        else if (v.scope == "upvalue")
            item->setForeground(0, QColor(0x7a, 0x4a, 0xa0));
    }
    vars_->resizeColumnToContents(0);
}

void ScriptDebugPanel::updateButtons()
{
    for (QToolButton* b : {continue_, over_, into_, out_})
        b->setEnabled(paused_);
    pause_btn_->setEnabled(running_ && !paused_);
}
