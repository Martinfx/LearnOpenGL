#pragma once
#include <engine/Scene.h>

#include <QTreeWidget>
#include <QWidget>
#include <filesystem>

class QLabel;
class QLineEdit;
class QMenu;
class QToolButton;

// Scene hierarchy: groups and objects as a tree. Drag an item onto another to
// make it a child (drop between items to move it next to them), F2 renames,
// the context menu creates and removes groups.
class HierarchyTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit HierarchyTree(engine::Scene* scene, QWidget* parent = nullptr);

    void rebuild();                   // after the scene changed
    void selectObject(int object);    // -1 clears
    void setFilter(const QString& text); // shows matching items and their parents
    static constexpr int kNameRole = Qt::UserRole;
    static constexpr int kObjectRole = Qt::UserRole + 1; // object index, -1 for groups

signals:
    void objectSelected(int object);
    void hierarchyChanged();          // parent, name or groups changed (level needs saving)
    void renamed();
    // Context menu on a group: "save" (as a new prefab), or on a prefab instance's
    // group "revert", "apply", "unpack", "open".
    void prefabRequested(const QString& group, const QString& action);

protected:
    void dropEvent(QDropEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    QTreeWidgetItem* itemForNode(const QString& name) const;
    void newGroup(const QString& parent);

    void applyFilter();

    engine::Scene* scene_;
    bool rebuilding_ = false;
    QString filter_;
};

// Outliner: search field and Add menu above the hierarchy, object count below.
class OutlinerPanel : public QWidget {
    Q_OBJECT
public:
    OutlinerPanel(HierarchyTree* tree, QWidget* parent = nullptr);
    void setAddMenu(QMenu* menu, const QIcon& icon);
    void setCounts(int objects, int selected);

private:
    HierarchyTree* tree_;
    QLineEdit* search_;
    QToolButton* addButton_;
    QLabel* count_;
};

// "What relates to what" for the selected object: its parent and children, the
// files it uses (model, script, materials, textures) and which other objects
// use the same files. Double-click opens the matching editor or selects the object.
class DependencyPanel : public QWidget {
    Q_OBJECT
public:
    explicit DependencyPanel(engine::Scene* scene, QWidget* parent = nullptr);
    void showObject(int object);

signals:
    void objectActivated(int object);
    void assetActivated(const QString& path, int kind); // kind = engine::AssetKind

private:
    engine::Scene* scene_;
    QLabel* title_;
    QTreeWidget* tree_;
};
