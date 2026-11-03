#include "StoryEditors.h"
#include "AssetPaths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QInputDialog>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <iomanip>
#include <set>
#include <sstream>

namespace {

std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdU16String()); }

std::vector<std::string> splitWords(const std::string& line)
{
    std::istringstream s(line);
    std::vector<std::string> w;
    for (std::string x; s >> std::quoted(x);)
        w.push_back(x);
    return w;
}

std::string joinWords(const std::vector<std::string>& w)
{
    std::string out;
    for (const std::string& x : w) {
        if (!out.empty())
            out += ' ';
        if (!x.empty() && x.find_first_of(" \t\"") == std::string::npos) {
            out += x;
        } else {
            std::ostringstream q;
            q << std::quoted(x);
            out += q.str();
        }
    }
    return out;
}

QString conditionsText(const std::vector<engine::DialogCondition>& list)
{
    QStringList lines;
    for (const auto& c : list)
        lines << QString::fromStdString((c.negate ? "not " : "") + joinWords(c.words));
    return lines.join('\n');
}

std::vector<engine::DialogCondition> parseConditions(const QString& text)
{
    std::vector<engine::DialogCondition> out;
    for (const QString& line : text.split('\n')) {
        std::vector<std::string> w = splitWords(line.toStdString());
        if (w.empty())
            continue;
        engine::DialogCondition c;
        if (w[0] == "not") {
            c.negate = true;
            w.erase(w.begin());
        }
        if (w.empty())
            continue;
        c.words = std::move(w);
        out.push_back(std::move(c));
    }
    return out;
}

QString actionsText(const std::vector<engine::DialogAction>& list)
{
    QStringList lines;
    for (const auto& a : list)
        lines << QString::fromStdString(joinWords(a.words));
    return lines.join('\n');
}

std::vector<engine::DialogAction> parseActions(const QString& text)
{
    std::vector<engine::DialogAction> out;
    for (const QString& line : text.split('\n'))
        if (std::vector<std::string> w = splitWords(line.toStdString()); !w.empty())
            out.push_back({std::move(w)});
    return out;
}

QPlainTextEdit* lines(QWidget* parent, const QString& tip, int rows)
{
    auto* e = new QPlainTextEdit(parent);
    e->setToolTip(tip);
    e->setTabChangesFocus(true);
    e->setFixedHeight(e->fontMetrics().lineSpacing() * rows + 12);
    return e;
}

const char* kConditionHelp = "One condition per line, all must hold:\n"
                             "  quest <id> none | active | done | failed | <stage id>\n"
                             "  flag <name>\n"
                             "\"not\" in front negates, e.g. not flag met_grisha";
const char* kActionHelp = "One action per line:\n"
                          "  start_quest <id>, advance_quest <id>, complete_quest <id>, fail_quest <id>\n"
                          "  quest_stage <id> <stage>, set_flag <name>, clear_flag <name>\n"
                          "  give medkits <n> | give ammo <type> <n> | give goodwill <faction> <n>\n"
                          "  fire <entity> <input> [parameter], hostile (the speaker attacks)";

} // namespace

QString StoryTab::title() const
{
    return QFileInfo(path_).fileName() + (modified_ ? "*" : "");
}

void StoryTab::setModified(bool on)
{
    if (modified_ == on)
        return;
    modified_ = on;
    emit modificationChanged();
}

void StoryTab::loadTexts(const std::filesystem::path& folder)
{
    texts_.clear();
    for (const std::string& language : engine::availableLanguages(folder)) {
        engine::StringTable table;
        if (table.load(folder / (language + ".strings")))
            texts_.emplace_back(language, std::move(table));
    }
}

QString StoryTab::shown(const std::string& text) const
{
    if (text.empty() || text[0] != '@' || texts_.empty())
        return QString::fromStdString(text);
    const auto& table = texts_.front().second.texts; // the base language (cs) comes first
    const auto it = table.find(text.substr(1));
    return it != table.end() ? QString::fromStdString(it->second) : QString::fromStdString(text);
}

QString StoryTab::preview(const std::string& text) const
{
    if (text.empty() || text[0] != '@')
        return QString();
    QString html = "<small>";
    for (const auto& [language, table] : texts_) {
        const auto it = table.texts.find(text.substr(1));
        html += "<b>" + QString::fromStdString(language) + "</b>: " +
                (it != table.texts.end() && !it->second.empty() ? QString::fromStdString(it->second).toHtmlEscaped()
                                                                 : "<span style='color:#e8a33c'>" + tr("missing") + "</span>") +
                "<br>";
    }
    return html + "</small>";
}

QLabel* StoryTab::previewLabel(QWidget* parent) const
{
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setToolTip(tr("\"@key\" texts come from assets/text/<language>.strings (Assets > Texts and translations)"));
    return label;
}


// ------------------------------------------------------------------ dialogs

DialogEditor::DialogEditor(QWidget* parent)
    : StoryTab(parent)
{
    quests_.loadFolder(toPath(assetPath("quests")));
    auto* layout = new QVBoxLayout(this);
    auto* top = new QFormLayout();
    speaker_ = new QLineEdit(this);
    speaker_->setToolTip(tr("Name shown above what the character says (empty = the entity's name)"));
    top->addRow(tr("Speaker"), speaker_);
    layout->addLayout(top);
    connect(speaker_, &QLineEdit::editingFinished, this, [this] {
        if (updating_ || speaker_->text().toStdString() == dialog_.speaker)
            return;
        dialog_.speaker = speaker_->text().toStdString();
        setModified(true);
    });

    auto* split = new QSplitter(this);
    layout->addWidget(split, 1);
    auto* left = new QWidget(split);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderLabels({tr("Node / answer"), tr("Text")});
    tree_->setToolTip(tr("Nodes are what the character says; the answers under a node are the player's choices. "
                         "The talk starts at the first node whose conditions hold."));
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    leftLayout->addWidget(tree_, 1);
    auto* buttons = new QHBoxLayout();
    auto button = [&](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, left);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    connect(button(tr("+ Node"), tr("A new node at the end")), &QPushButton::clicked, this, &DialogEditor::addNode);
    connect(button(tr("+ Answer"), tr("A new answer under the selected node")), &QPushButton::clicked, this, &DialogEditor::addChoice);
    connect(button(tr("Up"), tr("Move up (node order decides where a talk starts)")), &QPushButton::clicked, this,
            [this] { moveSelected(-1); });
    connect(button(tr("Down"), tr("Move down")), &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(button(tr("Delete"), tr("Delete the selected node or answer")), &QPushButton::clicked, this, &DialogEditor::removeSelected);
    leftLayout->addLayout(buttons);

    pages_ = new QStackedWidget(split);
    pages_->addWidget(new QLabel(tr("Select a node or an answer."), pages_));
    {
        auto* page = new QWidget(pages_);
        auto* form = new QFormLayout(page);
        nodeId_ = new QLineEdit(page);
        nodeId_->setToolTip(tr("Name answers lead to (unique in this dialog)"));
        nodeText_ = lines(page, tr("What the character says"), 5);
        nodeIf_ = lines(page, tr("Conditions to start the talk here.\n") + kConditionHelp, 3);
        nodeDo_ = lines(page, tr("Actions when this node is shown.\n") + kActionHelp, 3);
        form->addRow(tr("Node id"), nodeId_);
        form->addRow(tr("Says"), nodeText_);
        nodePreview_ = previewLabel(page);
        form->addRow(QString(), nodePreview_);
        form->addRow(tr("Starts if"), nodeIf_);
        form->addRow(tr("Does"), nodeDo_);
        form->addRow(new QLabel(tr("<small>Conditions: quest &lt;id&gt; none|active|done|failed|&lt;stage&gt;, flag &lt;name&gt;, "
                                   "not ...<br>Actions: start_quest, advance_quest, complete_quest, fail_quest &lt;id&gt;, "
                                   "quest_stage &lt;id&gt; &lt;stage&gt;, set_flag, clear_flag &lt;name&gt;, give medkits|ammo|goodwill ..., "
                                   "fire &lt;entity&gt; &lt;input&gt;, hostile</small>"),
                            page));
        pages_->addWidget(page);
        connect(nodeId_, &QLineEdit::editingFinished, this, &DialogEditor::edited);
        for (QPlainTextEdit* e : {nodeText_, nodeIf_, nodeDo_})
            connect(e, &QPlainTextEdit::textChanged, this, &DialogEditor::edited);
    }
    {
        auto* page = new QWidget(pages_);
        auto* form = new QFormLayout(page);
        choiceText_ = new QLineEdit(page);
        choiceText_->setToolTip(tr("What the player says"));
        choiceNext_ = new QComboBox(page);
        choiceNext_->setToolTip(tr("Node the talk goes on with; (end) closes the talk"));
        choiceIf_ = lines(page, tr("Conditions to offer this answer.\n") + kConditionHelp, 3);
        choiceDo_ = lines(page, tr("Actions when the player picks it.\n") + kActionHelp, 3);
        form->addRow(tr("Answer"), choiceText_);
        choicePreview_ = previewLabel(page);
        form->addRow(QString(), choicePreview_);
        form->addRow(tr("Leads to"), choiceNext_);
        form->addRow(tr("Offered if"), choiceIf_);
        form->addRow(tr("Does"), choiceDo_);
        pages_->addWidget(page);
        connect(choiceText_, &QLineEdit::editingFinished, this, &DialogEditor::edited);
        connect(choiceNext_, &QComboBox::activated, this, &DialogEditor::edited);
        for (QPlainTextEdit* e : {choiceIf_, choiceDo_})
            connect(e, &QPlainTextEdit::textChanged, this, &DialogEditor::edited);
    }
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    problems_ = new QLabel(this);
    problems_->setWordWrap(true);
    layout->addWidget(problems_);
    connect(tree_, &QTreeWidget::currentItemChanged, this, &DialogEditor::showSelection);
}

bool DialogEditor::load(const QString& path, QString* error)
{
    std::string e;
    if (!engine::loadDialog(dialog_, toPath(path), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    path_ = path;
    loadTexts(toPath(path).parent_path().parent_path() / "text");
    updating_ = true;
    speaker_->setText(QString::fromStdString(dialog_.speaker));
    updating_ = false;
    rebuildTree(0, -1);
    setModified(false);
    return true;
}

bool DialogEditor::save(QString* error)
{
    std::string e;
    if (!engine::saveDialog(dialog_, toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    setModified(false);
    return true;
}

std::pair<int, int> DialogEditor::selection() const
{
    const QTreeWidgetItem* item = tree_->currentItem();
    if (!item)
        return {-1, -1};
    if (const QTreeWidgetItem* parent = item->parent())
        return {tree_->indexOfTopLevelItem(const_cast<QTreeWidgetItem*>(parent)), parent->indexOfChild(const_cast<QTreeWidgetItem*>(item))};
    return {tree_->indexOfTopLevelItem(const_cast<QTreeWidgetItem*>(item)), -1};
}

void DialogEditor::rebuildTree(int selectNode, int selectChoice)
{
    updating_ = true;
    tree_->clear();
    for (const engine::DialogNode& n : dialog_.nodes) {
        auto* item = new QTreeWidgetItem(tree_, {QString::fromStdString(n.id), shown(n.text)});
        QFont bold = item->font(0);
        bold.setBold(true);
        item->setFont(0, bold);
        if (!n.conditions.empty())
            item->setToolTip(0, tr("Starts if: %1").arg(conditionsText(n.conditions)));
        for (const engine::DialogChoice& c : n.choices) {
            auto* child = new QTreeWidgetItem(item, {QString::fromStdString(c.next.empty() ? "→ (end)" : "→ " + c.next),
                                                     shown(c.text)});
            if (!c.conditions.empty())
                child->setToolTip(1, tr("Offered if: %1").arg(conditionsText(c.conditions)));
        }
        item->setExpanded(true);
    }
    updating_ = false;
    if (selectNode >= 0 && selectNode < tree_->topLevelItemCount()) {
        QTreeWidgetItem* item = tree_->topLevelItem(selectNode);
        if (selectChoice >= 0 && selectChoice < item->childCount())
            item = item->child(selectChoice);
        tree_->setCurrentItem(item);
    }
    showSelection();
    checkProblems();
}

void DialogEditor::showSelection()
{
    if (updating_)
        return;
    const auto [n, c] = selection();
    updating_ = true;
    if (n < 0) {
        pages_->setCurrentIndex(0);
    } else if (c < 0) {
        const engine::DialogNode& node = dialog_.nodes[static_cast<std::size_t>(n)];
        nodeId_->setText(QString::fromStdString(node.id));
        nodeText_->setPlainText(QString::fromStdString(node.text));
        nodePreview_->setText(preview(node.text));
        nodeIf_->setPlainText(conditionsText(node.conditions));
        nodeDo_->setPlainText(actionsText(node.actions));
        pages_->setCurrentIndex(1);
    } else {
        const engine::DialogChoice& choice = dialog_.nodes[static_cast<std::size_t>(n)].choices[static_cast<std::size_t>(c)];
        choiceText_->setText(QString::fromStdString(choice.text));
        choicePreview_->setText(preview(choice.text));
        choiceNext_->clear();
        choiceNext_->addItem(tr("(end)"), QString());
        for (const engine::DialogNode& other : dialog_.nodes)
            choiceNext_->addItem(QString::fromStdString(other.id), QString::fromStdString(other.id));
        int at = choiceNext_->findData(QString::fromStdString(choice.next));
        if (at < 0) {
            choiceNext_->addItem(QString::fromStdString(choice.next + " (missing)"), QString::fromStdString(choice.next));
            at = choiceNext_->count() - 1;
        }
        choiceNext_->setCurrentIndex(at);
        choiceIf_->setPlainText(conditionsText(choice.conditions));
        choiceDo_->setPlainText(actionsText(choice.actions));
        pages_->setCurrentIndex(2);
    }
    updating_ = false;
}

void DialogEditor::edited()
{
    if (updating_)
        return;
    const auto [n, c] = selection();
    if (n < 0)
        return;
    engine::DialogNode& node = dialog_.nodes[static_cast<std::size_t>(n)];
    QTreeWidgetItem* item = tree_->topLevelItem(n);
    if (c < 0) {
        const std::string id = nodeId_->text().trimmed().toStdString();
        if (!id.empty() && id != node.id && !dialog_.node(id)) {
            for (engine::DialogNode& other : dialog_.nodes) // answers follow the rename
                for (engine::DialogChoice& ch : other.choices)
                    if (ch.next == node.id)
                        ch.next = id;
            node.id = id;
        }
        node.text = nodeText_->toPlainText().toStdString();
        node.conditions = parseConditions(nodeIf_->toPlainText());
        node.actions = parseActions(nodeDo_->toPlainText());
        item->setText(0, QString::fromStdString(node.id));
        item->setText(1, shown(node.text));
        nodePreview_->setText(preview(node.text));
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) // renamed targets
            for (int k = 0; k < tree_->topLevelItem(i)->childCount(); ++k) {
                const std::string& next = dialog_.nodes[static_cast<std::size_t>(i)].choices[static_cast<std::size_t>(k)].next;
                tree_->topLevelItem(i)->child(k)->setText(0, QString::fromStdString(next.empty() ? "→ (end)" : "→ " + next));
            }
    } else {
        engine::DialogChoice& choice = node.choices[static_cast<std::size_t>(c)];
        choice.text = choiceText_->text().toStdString();
        choice.next = choiceNext_->currentData().toString().toStdString();
        choice.conditions = parseConditions(choiceIf_->toPlainText());
        choice.actions = parseActions(choiceDo_->toPlainText());
        item->child(c)->setText(0, QString::fromStdString(choice.next.empty() ? "→ (end)" : "→ " + choice.next));
        item->child(c)->setText(1, shown(choice.text));
        choicePreview_->setText(preview(choice.text));
    }
    setModified(true);
    checkProblems();
}

void DialogEditor::checkProblems()
{
    const std::vector<std::string> problems = engine::checkDialog(dialog_, &quests_);
    if (problems.empty()) {
        problems_->setText(tr("<span style='color:#6a6'>No problems.</span>"));
        return;
    }
    QStringList lines;
    for (const std::string& p : problems)
        lines << QString::fromStdString(p).toHtmlEscaped();
    problems_->setText("<span style='color:#d66'>" + lines.join("<br>") + "</span>");
}

void DialogEditor::addNode()
{
    engine::DialogNode n;
    int i = static_cast<int>(dialog_.nodes.size()) + 1;
    do
        n.id = "node" + std::to_string(i++);
    while (dialog_.node(n.id));
    n.text = "...";
    dialog_.nodes.push_back(n);
    setModified(true);
    rebuildTree(static_cast<int>(dialog_.nodes.size()) - 1, -1);
}

void DialogEditor::addChoice()
{
    const int n = selection().first;
    if (n < 0)
        return;
    auto& choices = dialog_.nodes[static_cast<std::size_t>(n)].choices;
    choices.push_back({"...", "", {}, {}});
    setModified(true);
    rebuildTree(n, static_cast<int>(choices.size()) - 1);
}

void DialogEditor::removeSelected()
{
    const auto [n, c] = selection();
    if (n < 0)
        return;
    if (c >= 0) {
        auto& choices = dialog_.nodes[static_cast<std::size_t>(n)].choices;
        choices.erase(choices.begin() + c);
        rebuildTree(n, std::min(c, static_cast<int>(choices.size()) - 1));
    } else {
        if (dialog_.nodes.size() <= 1)
            return; // a dialog needs a node
        dialog_.nodes.erase(dialog_.nodes.begin() + n);
        rebuildTree(std::min(n, static_cast<int>(dialog_.nodes.size()) - 1), -1);
    }
    setModified(true);
}

void DialogEditor::moveSelected(int delta)
{
    const auto [n, c] = selection();
    if (n < 0)
        return;
    if (c >= 0) {
        auto& choices = dialog_.nodes[static_cast<std::size_t>(n)].choices;
        const int to = c + delta;
        if (to < 0 || to >= static_cast<int>(choices.size()))
            return;
        std::swap(choices[static_cast<std::size_t>(c)], choices[static_cast<std::size_t>(to)]);
        rebuildTree(n, to);
    } else {
        const int to = n + delta;
        if (to < 0 || to >= static_cast<int>(dialog_.nodes.size()))
            return;
        std::swap(dialog_.nodes[static_cast<std::size_t>(n)], dialog_.nodes[static_cast<std::size_t>(to)]);
        rebuildTree(to, -1);
    }
    setModified(true);
}

// ------------------------------------------------------------------ quests

QuestEditor::QuestEditor(QWidget* parent)
    : StoryTab(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);
    id_ = new QLineEdit(this);
    id_->setToolTip(tr("Name dialogs and scripts use (game.quest(\"camp_stash\"))"));
    title_ = new QLineEdit(this);
    title_->setToolTip(tr("Name in the PDA and the HUD"));
    description_ = lines(this, tr("Longer text in the PDA"), 3);
    giver_ = new QLineEdit(this);
    giver_->setToolTip(tr("Entity name of the character who gives it"));
    form->addRow(tr("Id"), id_);
    form->addRow(tr("Title"), title_);
    titlePreview_ = previewLabel(this);
    form->addRow(QString(), titlePreview_);
    form->addRow(tr("Description"), description_);
    descriptionPreview_ = previewLabel(this);
    form->addRow(QString(), descriptionPreview_);
    form->addRow(tr("Given by"), giver_);
    stages_ = new QTableWidget(0, 3, this);
    stages_->setHorizontalHeaderLabels({tr("Stage id"), tr("Text in the PDA"), tr("Target entity (map)")});
    stages_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    stages_->setToolTip(tr("Stages in order: the quest starts at the first, objectives and dialogs move it on; "
                           "after the last one it is done. The target entity is marked on the PDA map and the minimap."));
    form->addRow(tr("Stages"), stages_);
    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(tr("+ Stage"), this);
    auto* remove = new QPushButton(tr("Delete stage"), this);
    auto* up = new QPushButton(tr("Up"), this);
    auto* down = new QPushButton(tr("Down"), this);
    for (QPushButton* b : {add, remove, up, down})
        buttons->addWidget(b);
    buttons->addStretch();
    form->addRow(QString(), buttons);
    rewards_ = lines(this, tr("One reward per line, given when the quest is done:\n  medkits <n>\n  ammo <type> <n>   e.g. ammo 5.45x39 60\n"
                              "  goodwill <faction> <n>\n  money <roubles>"),
                     4);
    form->addRow(tr("Rewards"), rewards_);
    layout->addStretch();

    for (QLineEdit* e : {id_, title_, giver_})
        connect(e, &QLineEdit::editingFinished, this, &QuestEditor::edited);
    connect(description_, &QPlainTextEdit::textChanged, this, &QuestEditor::edited);
    connect(rewards_, &QPlainTextEdit::textChanged, this, &QuestEditor::edited);
    connect(stages_, &QTableWidget::itemChanged, this, [this] {
        if (!updating_)
            edited();
    });
    connect(add, &QPushButton::clicked, this, [this] {
        readStages();
        quest_.stages.push_back({"stage" + std::to_string(quest_.stages.size() + 1), "...", ""});
        refresh();
        setModified(true);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        readStages();
        const int r = stages_->currentRow();
        if (r < 0 || quest_.stages.size() <= 1)
            return;
        quest_.stages.erase(quest_.stages.begin() + r);
        refresh();
        setModified(true);
    });
    auto move = [this](int delta) {
        readStages();
        const int r = stages_->currentRow(), to = r + delta;
        if (r < 0 || to < 0 || to >= static_cast<int>(quest_.stages.size()))
            return;
        std::swap(quest_.stages[static_cast<std::size_t>(r)], quest_.stages[static_cast<std::size_t>(to)]);
        refresh();
        stages_->setCurrentCell(to, 0);
        setModified(true);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
}

bool QuestEditor::load(const QString& path, QString* error)
{
    std::string e;
    if (!engine::loadQuest(quest_, toPath(path), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    path_ = path;
    loadTexts(toPath(path).parent_path().parent_path() / "text");
    refresh();
    setModified(false);
    return true;
}

bool QuestEditor::save(QString* error)
{
    readStages();
    std::string e;
    if (!engine::saveQuest(quest_, toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    setModified(false);
    return true;
}

void QuestEditor::refresh()
{
    updating_ = true;
    id_->setText(QString::fromStdString(quest_.id));
    title_->setText(QString::fromStdString(quest_.title));
    description_->setPlainText(QString::fromStdString(quest_.description));
    giver_->setText(QString::fromStdString(quest_.giver));
    stages_->setRowCount(static_cast<int>(quest_.stages.size()));
    for (int r = 0; r < static_cast<int>(quest_.stages.size()); ++r) {
        const engine::QuestStage& s = quest_.stages[static_cast<std::size_t>(r)];
        stages_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(s.id)));
        auto* text = new QTableWidgetItem(QString::fromStdString(s.text));
        text->setToolTip(preview(s.text));
        stages_->setItem(r, 1, text);
        stages_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(s.target)));
    }
    QStringList rewards;
    for (const auto& r : quest_.rewards)
        rewards << QString::fromStdString(joinWords(r));
    rewards_->setPlainText(rewards.join('\n'));
    titlePreview_->setText(preview(quest_.title));
    descriptionPreview_->setText(preview(quest_.description));
    updating_ = false;
}

void QuestEditor::readStages()
{
    quest_.stages.clear();
    for (int r = 0; r < stages_->rowCount(); ++r) {
        auto text = [&](int c) { return stages_->item(r, c) ? stages_->item(r, c)->text().trimmed().toStdString() : std::string(); };
        if (!text(0).empty())
            quest_.stages.push_back({text(0), text(1), text(2)});
    }
}

void QuestEditor::edited()
{
    if (updating_)
        return;
    quest_.id = id_->text().trimmed().toStdString();
    quest_.title = title_->text().toStdString();
    quest_.description = description_->toPlainText().toStdString();
    quest_.giver = giver_->text().trimmed().toStdString();
    readStages();
    titlePreview_->setText(preview(quest_.title));
    descriptionPreview_->setText(preview(quest_.description));
    quest_.rewards.clear();
    for (const QString& line : rewards_->toPlainText().split('\n'))
        if (auto w = splitWords(line.toStdString()); !w.empty())
            quest_.rewards.push_back(std::move(w));
    setModified(true);
}

ItemsEditor::ItemsEditor(QWidget* parent)
    : StoryTab(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);
    money_ = new QSpinBox(this);
    money_->setRange(0, 10000000);
    money_->setSuffix(tr(" RU"));
    money_->setToolTip(tr("Roubles the player starts a new game with"));
    carryFree_ = new QDoubleSpinBox(this);
    carryMax_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* d : {carryFree_, carryMax_}) {
        d->setRange(1, 1000);
        d->setSuffix(tr(" kg"));
    }
    carryFree_->setToolTip(tr("Up to this load the player moves normally; above it no sprint and slower"));
    carryMax_->setToolTip(tr("At this load the player crawls (30 % speed)"));
    form->addRow(tr("Start money"), money_);
    form->addRow(tr("Free load"), carryFree_);
    form->addRow(tr("Maximum load"), carryMax_);
    table_ = new QTableWidget(0, 12, this);
    table_->setHorizontalHeaderLabels({tr("Kind"), tr("Id"), tr("Name"), tr("Price (RU)"), tr("Weight (kg)"), tr("Protection"), tr("Wear"),
                                       tr("Grows in"), tr("Model"), tr("Glow (r g b)"), tr("Heal / rad / carry"), tr("Trade lot")});
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setToolTip(tr("Kind: medkit, ammo (id = ammo type of the weapons, price and weight per round), armor "
                          "(protection = share of damage it stops when new, wear = condition lost per hp stopped) or artifact "
                          "(grows in = anomaly kind: crusher, discharge, vent, acid, rift; model relative to assets/; "
                          "protection = share of damage stopped on the belt; heal hp/s, rad rad/s (negative draws the dose out), carry kg). "
                          "Trade lot: ammo rounds bought or sold per click (30; 1 for bottles)"));
    layout->addWidget(table_, 1);
    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(tr("+ Item"), this);
    auto* remove = new QPushButton(tr("Delete item"), this);
    auto* belt = new QSpinBox(this);
    belt->setRange(0, 10);
    belt->setPrefix(tr("Belt: "));
    belt->setSuffix(tr(" artifacts"));
    belt->setToolTip(tr("How many carried artifacts work at once (the first ones picked up)"));
    belt_ = belt;
    buttons->addWidget(belt);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(money_, &QSpinBox::valueChanged, this, &ItemsEditor::edited);
    connect(belt_, &QSpinBox::valueChanged, this, &ItemsEditor::edited);
    connect(carryFree_, &QDoubleSpinBox::valueChanged, this, &ItemsEditor::edited);
    connect(carryMax_, &QDoubleSpinBox::valueChanged, this, &ItemsEditor::edited);
    connect(table_, &QTableWidget::itemChanged, this, &ItemsEditor::edited);
    connect(add, &QPushButton::clicked, this, [this] {
        engine::ItemDef d;
        d.kind = engine::ItemDef::Kind::Armor;
        d.id = "armor" + std::to_string(items_.items.size() + 1);
        d.name = "...";
        d.price = 1000;
        d.weight = 3.0f;
        d.protection = 0.2f;
        d.wear = 0.004f;
        items_.items.push_back(d);
        refresh();
        setModified(true);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int r = table_->currentRow();
        if (r < 0 || r >= static_cast<int>(items_.items.size()))
            return;
        items_.items.erase(items_.items.begin() + r);
        refresh();
        setModified(true);
    });
}

bool ItemsEditor::load(const QString& path, QString* error)
{
    std::string e;
    if (!engine::loadItems(items_, toPath(path), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    path_ = path;
    loadTexts(toPath(path).parent_path() / "text");
    refresh();
    setModified(false);
    return true;
}

bool ItemsEditor::save(QString* error)
{
    std::string e;
    if (!engine::saveItems(items_, toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    setModified(false);
    return true;
}

namespace {
const char* kindName(engine::ItemDef::Kind k)
{
    return k == engine::ItemDef::Kind::Medkit ? "medkit" : k == engine::ItemDef::Kind::Ammo ? "ammo" : k == engine::ItemDef::Kind::Artifact ? "artifact" : "armor";
}
} // namespace

void ItemsEditor::refresh()
{
    updating_ = true;
    money_->setValue(items_.moneyStart);
    carryFree_->setValue(items_.carryFree);
    carryMax_->setValue(items_.carryMax);
    belt_->setValue(items_.beltSlots);
    table_->setRowCount(static_cast<int>(items_.items.size()));
    for (int r = 0; r < static_cast<int>(items_.items.size()); ++r) {
        const engine::ItemDef& d = items_.items[static_cast<std::size_t>(r)];
        const bool armor = d.kind == engine::ItemDef::Kind::Armor, art = d.kind == engine::ItemDef::Kind::Artifact;
        const QStringList cells = {kindName(d.kind), QString::fromStdString(d.id), QString::fromStdString(d.name), QString::number(d.price),
                                   QString::number(d.weight), armor || art ? QString::number(d.protection) : QString(),
                                   armor ? QString::number(d.wear) : QString(), art ? QString::fromStdString(d.anomaly) : QString(),
                                   art ? QString::fromStdString(d.model) : QString(),
                                   art ? QStringLiteral("%1 %2 %3").arg(d.glow[0]).arg(d.glow[1]).arg(d.glow[2]) : QString(),
                                   art ? QStringLiteral("%1 %2 %3").arg(d.heal).arg(d.rad).arg(d.carry) : QString(),
                                   d.kind == engine::ItemDef::Kind::Ammo ? QString::number(d.lot) : QString()};
        for (int c = 0; c < cells.size(); ++c)
            table_->setItem(r, c, new QTableWidgetItem(cells[c]));
        table_->item(r, 2)->setToolTip(preview(d.name));
    }
    updating_ = false;
}

void ItemsEditor::edited()
{
    if (updating_)
        return;
    items_.moneyStart = money_->value();
    items_.carryFree = static_cast<float>(carryFree_->value());
    items_.carryMax = static_cast<float>(std::max(carryMax_->value(), carryFree_->value() + 1.0));
    items_.beltSlots = belt_->value();
    std::vector<engine::ItemDef> list;
    for (int r = 0; r < table_->rowCount(); ++r) {
        auto text = [&](int c) { return table_->item(r, c) ? table_->item(r, c)->text().trimmed() : QString(); };
        engine::ItemDef d;
        const QString kind = text(0).toLower();
        d.kind = kind == "medkit"     ? engine::ItemDef::Kind::Medkit
                 : kind == "ammo"     ? engine::ItemDef::Kind::Ammo
                 : kind == "artifact" ? engine::ItemDef::Kind::Artifact
                                      : engine::ItemDef::Kind::Armor;
        d.id = d.kind == engine::ItemDef::Kind::Medkit ? std::string("medkit") : text(1).toStdString();
        d.name = text(2).toStdString();
        d.price = std::max(0, text(3).toInt());
        d.weight = std::max(0.0f, text(4).toFloat());
        if (d.kind == engine::ItemDef::Kind::Ammo)
            d.lot = text(11).isEmpty() ? 30 : std::max(1, text(11).toInt());
        if (d.kind == engine::ItemDef::Kind::Armor) {
            d.protection = std::clamp(text(5).toFloat(), 0.0f, 0.95f);
            d.wear = std::max(0.0f, text(6).toFloat());
        } else if (d.kind == engine::ItemDef::Kind::Artifact) {
            d.protection = std::clamp(text(5).toFloat(), -1.0f, 0.95f);
            d.anomaly = text(7).toLower().toStdString();
            d.model = text(8).toStdString();
            const QStringList glow = text(9).split(' ', Qt::SkipEmptyParts), fx = text(10).split(' ', Qt::SkipEmptyParts);
            for (int k = 0; k < 3; ++k) {
                if (k < glow.size())
                    d.glow[k] = std::clamp(glow[k].toFloat(), 0.0f, 4.0f);
            }
            d.heal = fx.size() > 0 ? fx[0].toFloat() : 0.0f;
            d.rad = fx.size() > 1 ? fx[1].toFloat() : 0.0f;
            d.carry = fx.size() > 2 ? fx[2].toFloat() : 0.0f;
        }
        list.push_back(std::move(d));
    }
    items_.items = std::move(list);
    setModified(true);
}

bool writeNewDialog(const QString& path, QString* error)
{
    engine::DialogDef d;
    d.speaker = "";
    engine::DialogNode n;
    n.id = "start";
    n.text = "Co chceš?";
    n.choices.push_back({"Nic, měj se.", "", {}, {}});
    d.nodes.push_back(n);
    std::string e;
    const bool ok = engine::saveDialog(d, toPath(path), &e);
    if (!ok && error)
        *error = QString::fromStdString(e);
    return ok;
}

bool writeNewQuest(const QString& path, QString* error)
{
    engine::QuestDef q;
    q.id = QFileInfo(path).completeBaseName().toStdString();
    q.title = "Nový úkol";
    q.stages.push_back({"start", "Co má hráč udělat", ""});
    std::string e;
    const bool ok = engine::saveQuest(q, toPath(path), &e);
    if (!ok && error)
        *error = QString::fromStdString(e);
    return ok;
}

// ------------------------------------------------------------------ texts

StringsEditor::StringsEditor(QWidget* parent)
    : StoryTab(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter keys and texts"));
    onlyMissing_ = new QCheckBox(tr("Only untranslated"), this);
    auto* add = new QPushButton(tr("Add key"), this);
    auto* remove = new QPushButton(tr("Remove key"), this);
    auto* language = new QPushButton(tr("Add language..."), this);
    bar->addWidget(filter_, 1);
    bar->addWidget(onlyMissing_);
    bar->addWidget(add);
    bar->addWidget(remove);
    bar->addWidget(language);
    layout->addLayout(bar);
    auto* help = new QLabel(tr("Keys are used by the game (menu.back) and by data as @key (dialogs, quests, level titles). "
                               "{0} is an argument, key.one / .few / .other are plural forms. Missing texts fall back to Czech."),
                            this);
    help->setWordWrap(true);
    layout->addWidget(help);
    table_ = new QTableWidget(this);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->hide();
    layout->addWidget(table_, 1);
    status_ = new QLabel(this);
    layout->addWidget(status_);
    connect(filter_, &QLineEdit::textChanged, this, &StringsEditor::refresh);
    connect(onlyMissing_, &QCheckBox::toggled, this, &StringsEditor::refresh);
    connect(add, &QPushButton::clicked, this, &StringsEditor::addKey);
    connect(remove, &QPushButton::clicked, this, &StringsEditor::removeKey);
    connect(language, &QPushButton::clicked, this, &StringsEditor::addLanguage);
    connect(table_, &QTableWidget::cellChanged, this, &StringsEditor::cellEdited);
}

bool StringsEditor::load(const QString& folder, QString* error)
{
    path_ = folder;
    languages_ = engine::availableLanguages(std::filesystem::path(folder.toStdU16String()));
    if (languages_.empty() || languages_.front() != engine::Localization::kBase)
        languages_.insert(languages_.begin(), engine::Localization::kBase);
    tables_.clear();
    for (const std::string& lang : languages_) {
        const auto file = std::filesystem::path(folder.toStdU16String()) / (lang + ".strings");
        std::string err;
        if (std::filesystem::exists(file) && !tables_[lang].load(file, &err)) {
            if (error)
                *error = QString::fromStdString(err);
            return false;
        }
    }
    refresh();
    return true;
}

void StringsEditor::refresh()
{
    updating_ = true;
    std::set<std::string> keys;
    for (const auto& [lang, t] : tables_)
        for (const auto& [k, v] : t.texts)
            keys.insert(k);
    std::map<std::string, std::set<std::string>> missing; // language -> keys
    for (std::size_t l = 1; l < languages_.size(); ++l)
        for (const std::string& k : engine::untranslatedKeys(tables_[languages_[0]], tables_[languages_[l]], languages_[l]))
            missing[languages_[l]].insert(k);
    QStringList headers{tr("Key")};
    for (const std::string& lang : languages_)
        headers << QString::fromStdString(lang);
    table_->clear();
    table_->setColumnCount(static_cast<int>(headers.size()));
    table_->setHorizontalHeaderLabels(headers);
    const QString filter = filter_->text().trimmed();
    std::vector<std::string> shown;
    for (const std::string& k : keys) {
        bool untranslated = false;
        for (const auto& [lang, set] : missing)
            untranslated = untranslated || set.count(k);
        if (onlyMissing_->isChecked() && !untranslated)
            continue;
        bool match = filter.isEmpty() || QString::fromStdString(k).contains(filter, Qt::CaseInsensitive);
        for (const std::string& lang : languages_) {
            const auto& texts = tables_[lang].texts;
            if (auto it = texts.find(k); !match && it != texts.end())
                match = QString::fromStdString(it->second).contains(filter, Qt::CaseInsensitive);
        }
        if (match)
            shown.push_back(k);
    }
    table_->setRowCount(static_cast<int>(shown.size()));
    for (int r = 0; r < static_cast<int>(shown.size()); ++r) {
        const std::string& k = shown[static_cast<std::size_t>(r)];
        auto* keyItem = new QTableWidgetItem(QString::fromStdString(k));
        keyItem->setData(Qt::UserRole, QString::fromStdString(k));
        table_->setItem(r, 0, keyItem);
        for (std::size_t l = 0; l < languages_.size(); ++l) {
            const auto& texts = tables_[languages_[l]].texts;
            const auto it = texts.find(k);
            auto* item = new QTableWidgetItem(it == texts.end() ? QString() : QString::fromStdString(it->second).replace('\n', "\\n"));
            if (l > 0 && missing[languages_[l]].count(k))
                item->setBackground(QColor(0x6b, 0x55, 0x10)); // untranslated
            table_->setItem(r, static_cast<int>(l) + 1, item);
        }
    }
    table_->resizeColumnToContents(0);
    QString text = tr("%1 keys").arg(keys.size());
    for (std::size_t l = 1; l < languages_.size(); ++l)
        text += tr(" · %1: %2 untranslated").arg(QString::fromStdString(languages_[l])).arg(missing[languages_[l]].size());
    status_->setText(text);
    updating_ = false;
}

void StringsEditor::cellEdited(int row, int column)
{
    if (updating_)
        return;
    QTableWidgetItem* keyItem = table_->item(row, 0);
    if (!keyItem)
        return;
    const std::string oldKey = keyItem->data(Qt::UserRole).toString().toStdString();
    if (column == 0) { // renamed key: in every language
        const std::string newKey = keyItem->text().trimmed().toStdString();
        if (newKey.empty() || newKey == oldKey)
            return refresh();
        for (auto& [lang, t] : tables_)
            if (auto it = t.texts.find(oldKey); it != t.texts.end()) {
                t.texts[newKey] = it->second;
                t.texts.erase(oldKey);
            }
    } else {
        QString text = table_->item(row, column)->text();
        tables_[languages_[static_cast<std::size_t>(column - 1)]].texts[oldKey] = text.replace("\\n", "\n").toStdString();
    }
    setModified(true);
    markUntranslated();
}

void StringsEditor::markUntranslated()
{
    // Colours and counts follow the edit without rebuilding the table (keeps the cursor).
    updating_ = true;
    for (std::size_t l = 1; l < languages_.size(); ++l) {
        const auto list = engine::untranslatedKeys(tables_[languages_[0]], tables_[languages_[l]], languages_[l]);
        const std::set<std::string> missing(list.begin(), list.end());
        for (int r = 0; r < table_->rowCount(); ++r)
            if (QTableWidgetItem* item = table_->item(r, static_cast<int>(l) + 1); item && table_->item(r, 0))
                item->setBackground(missing.count(table_->item(r, 0)->data(Qt::UserRole).toString().toStdString())
                                        ? QBrush(QColor(0x6b, 0x55, 0x10))
                                        : QBrush());
    }
    updating_ = false;
}

void StringsEditor::addKey()
{
    bool ok = false;
    const QString key = QInputDialog::getText(this, tr("Add key"), tr("Key (e.g. hud.medkit_used):"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || key.isEmpty())
        return;
    tables_[engine::Localization::kBase].texts[key.toStdString()]; // empty Czech text to fill in
    setModified(true);
    filter_->setText(key);
    refresh();
}

void StringsEditor::removeKey()
{
    const int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 0))
        return;
    const std::string key = table_->item(row, 0)->data(Qt::UserRole).toString().toStdString();
    for (auto& [lang, t] : tables_)
        t.texts.erase(key);
    setModified(true);
    refresh();
}

void StringsEditor::addLanguage()
{
    bool ok = false;
    const QString code = QInputDialog::getText(this, tr("Add language"), tr("Language code (e.g. de, pl, sk):"), QLineEdit::Normal,
                                               QString(), &ok).trimmed().toLower();
    if (!ok || code.isEmpty() || std::find(languages_.begin(), languages_.end(), code.toStdString()) != languages_.end())
        return;
    languages_.push_back(code.toStdString());
    tables_[code.toStdString()].texts["language." + code.toStdString()]; // its own name, to fill in
    tables_[engine::Localization::kBase].texts["language." + code.toStdString()];
    setModified(true);
    refresh();
}

bool StringsEditor::save(QString* error)
{
    for (const std::string& lang : languages_) {
        std::string err;
        if (!tables_[lang].save(std::filesystem::path(path_.toStdU16String()) / (lang + ".strings"), &err)) {
            if (error)
                *error = QString::fromStdString(err);
            return false;
        }
    }
    setModified(false);
    return true;
}

