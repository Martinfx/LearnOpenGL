#pragma once
#include <engine/Dialog.h>
#include <engine/Items.h>
#include <engine/Localization.h>
#include <engine/Quests.h>

#include <QWidget>

#include <filesystem>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Editor tabs for the story data: dialogs (assets/dialogs/*.dialog) as a tree of nodes and
// answers, and quests (assets/quests/*.quest) with their stages and rewards. Both share this
// base so the main window saves and closes them the same way.
class StoryTab : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual bool save(QString* error) = 0;
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();

protected:
    void setModified(bool on);
    // "@key" texts: what they say in each language. `textFolder` is assets/text (next to the
    // dialogs, quests and items.cfg).
    void loadTexts(const std::filesystem::path& textFolder);
    QString shown(const std::string& text) const;   // the base language text of a key, else as written
    QString preview(const std::string& text) const; // every language, for labels and tooltips; empty if not a key
    QLabel* previewLabel(QWidget* parent) const;
    std::vector<std::pair<std::string, engine::StringTable>> texts_;
    QString path_;
    bool modified_ = false;
    bool updating_ = false;
};

// A conversation: nodes (what the character says) with answers under them, each answer leading
// to another node or ending the talk. Conditions and actions are one per line, e.g.
// `quest camp_stash none`, `not flag met_grisha`, `start_quest camp_stash`, `give medkits 1`.
// Problems (an answer leading nowhere, an unknown quest) are listed at the bottom.
class DialogEditor : public StoryTab {
    Q_OBJECT
public:
    explicit DialogEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error) override;

private:
    void rebuildTree(int selectNode = -1, int selectChoice = -1);
    void showSelection();
    void edited();
    void checkProblems();
    void addNode();
    void addChoice();
    void removeSelected();
    void moveSelected(int delta);
    // The selection: node index, choice index (-1 = the node itself).
    std::pair<int, int> selection() const;

    engine::DialogDef dialog_;
    engine::Quests quests_; // for the checks and the quest pickers
    QLineEdit* speaker_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    // Node page.
    QLineEdit* nodeId_ = nullptr;
    QPlainTextEdit* nodeText_ = nullptr;
    QPlainTextEdit* nodeIf_ = nullptr;
    QPlainTextEdit* nodeDo_ = nullptr;
    // Answer page.
    QLineEdit* choiceText_ = nullptr;
    QComboBox* choiceNext_ = nullptr;
    QPlainTextEdit* choiceIf_ = nullptr;
    QPlainTextEdit* choiceDo_ = nullptr;
    QLabel* problems_ = nullptr;
    QLabel* nodePreview_ = nullptr;
    QLabel* choicePreview_ = nullptr;
};

// A quest: id, title, description for the PDA, who gives it, the stages in order (text in the
// PDA and the entity marked on the map) and the rewards on completion.
class QuestEditor : public StoryTab {
    Q_OBJECT
public:
    explicit QuestEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error) override;

private:
    void refresh();
    void edited();
    void readStages();

    engine::QuestDef quest_;
    QLineEdit* id_ = nullptr;
    QLineEdit* title_ = nullptr;
    QPlainTextEdit* description_ = nullptr;
    QLineEdit* giver_ = nullptr;
    QTableWidget* stages_ = nullptr;
    QPlainTextEdit* rewards_ = nullptr;
    QLabel* titlePreview_ = nullptr;
    QLabel* descriptionPreview_ = nullptr;
};

// The item catalog (assets/items.cfg): start money, carry limits, belt size and every medkit, ammo
// type, armor and artifact with its price (traders use it with their markup) and weight (load). Weapons keep their
// price and weight in their .weapon files (Weapons panel).
class ItemsEditor : public StoryTab {
    Q_OBJECT
public:
    explicit ItemsEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error) override;

private:
    void refresh();
    void edited();

    engine::ItemCatalog items_;
    class QSpinBox* money_ = nullptr;
    class QDoubleSpinBox* carryFree_ = nullptr;
    class QDoubleSpinBox* carryMax_ = nullptr;
    class QSpinBox* belt_ = nullptr;
    QTableWidget* table_ = nullptr;
};

// New files with a small valid start (assets/dialogs/<name>.dialog, assets/quests/<name>.quest).
bool writeNewDialog(const QString& path, QString* error);
bool writeNewQuest(const QString& path, QString* error);

// Texts and translations (assets/text/<language>.strings): one row per key, one column per
// language. Cells a language still needs are amber (untranslated); "Only untranslated" hides
// the rest. Czech (cs) is the base every other language falls back to.
class StringsEditor : public StoryTab {
    Q_OBJECT
public:
    explicit StringsEditor(QWidget* parent = nullptr);
    bool load(const QString& folder, QString* error);
    bool save(QString* error) override;

private:
    void refresh();
    void cellEdited(int row, int column);
    void addKey();
    void removeKey();
    void addLanguage();
    void markUntranslated();

    std::vector<std::string> languages_;           // cs first
    std::map<std::string, engine::StringTable> tables_;
    QTableWidget* table_ = nullptr;
    QLineEdit* filter_ = nullptr;
    class QCheckBox* onlyMissing_ = nullptr;
    QLabel* status_ = nullptr;
};

