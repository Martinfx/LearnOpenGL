#include "HierarchyPanel.h"

#include <engine/SceneGraph.h>

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include <QLineEdit>
#include <QToolButton>

#include <functional>
#include <map>

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
QString qpath(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }

QString kindTag(const engine::Scene& scene, int object)
{
    if (scene.isBox(object))
        return QObject::tr("box");
    if (scene.isModel(object))
        return QObject::tr("glTF");
    return qs(scene.entity(object).type);
}

QColor kindColor(const engine::Scene& scene, int object)
{
    if (object < 0)
        return QColor(200, 160, 60);   // group
    if (scene.isBox(object))
        return QColor(120, 140, 170);
    if (scene.isModel(object))
        return QColor(90, 170, 110);
    return QColor(200, 90, 80);
}
} // namespace

HierarchyTree::HierarchyTree(engine::Scene* scene, QWidget* parent) : QTreeWidget(parent), scene_(scene)
{
    setHeaderLabels({tr("Item Label"), tr("Type")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setExpandsOnDoubleClick(false);

    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!rebuilding_)
            emit objectSelected(item ? item->data(0, kObjectRole).toInt() : -1);
    });
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (rebuilding_ || column != 0)
            return;
        const std::string oldName = item->data(0, kNameRole).toString().toStdString();
        const std::string newName = item->text(0).trimmed().toStdString();
        if (newName == oldName)
            return;
        if (!engine::renameNode(*scene_, oldName, newName)) {
            QMessageBox::warning(this, tr("Rename"),
                                 tr("The name \"%1\" is empty or already used. Names must be unique so children can "
                                    "refer to their parent.").arg(qs(newName)));
            rebuild();
            return;
        }
        rebuild();
        emit renamed();
        emit hierarchyChanged();
    });
}

QTreeWidgetItem* HierarchyTree::itemForNode(const QString& name) const
{
    QTreeWidgetItemIterator it(const_cast<HierarchyTree*>(this));
    for (; *it; ++it)
        if ((*it)->data(0, kNameRole).toString() == name)
            return *it;
    return nullptr;
}

void HierarchyTree::rebuild()
{
    rebuilding_ = true;
    const int selected = currentItem() ? currentItem()->data(0, kObjectRole).toInt() : -2;
    const QString selectedName = currentItem() ? currentItem()->data(0, kNameRole).toString() : QString();
    // Remember collapsed nodes so a rebuild keeps the user's view.
    std::map<QString, bool> collapsed;
    for (QTreeWidgetItemIterator it(this); *it; ++it)
        if ((*it)->childCount() > 0 && !(*it)->isExpanded())
            collapsed[(*it)->data(0, kNameRole).toString()] = true;
    clear();

    std::map<std::string, QTreeWidgetItem*> items;
    std::vector<std::pair<std::string, QTreeWidgetItem*>> pending; // (parent name, item)
    auto make = [&](const std::string& name, const std::string& parent, int object, const QString& kind) {
        auto* item = new QTreeWidgetItem({qs(name), kind});
        item->setData(0, kNameRole, qs(name));
        item->setData(0, kObjectRole, object);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        item->setForeground(1, kindColor(*scene_, object));
        if (object < 0) {
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
        }
        if (!items.count(name))
            items[name] = item;
        pending.emplace_back(parent, item);
    };
    for (const engine::Group& g : scene_->groups) {
        const bool instance = !g.prefabInstance.empty() && g.prefabInstance == g.name;
        make(g.name, g.parent, -1, instance ? tr("prefab") : tr("group"));
        if (instance) // prefab instances in blue, like Unreal
            pending.back().second->setForeground(0, QColor(110, 170, 255));
    }
    for (int i = 0; i < scene_->objectCount(); ++i)
        make(engine::objectName(*scene_, i), engine::objectParent(*scene_, i), i, kindTag(*scene_, i));
    for (auto& [parent, item] : pending) {
        const auto it = items.find(parent);
        // Missing parents and cycles (broken files) fall back to the root.
        const bool valid = !parent.empty() && it != items.end() && it->second != item &&
                           !engine::isAncestor(*scene_, item->data(0, kNameRole).toString().toStdString(), parent);
        if (valid)
            it->second->addChild(item);
        else
            addTopLevelItem(item);
    }
    for (QTreeWidgetItemIterator it(this); *it; ++it)
        (*it)->setExpanded(!collapsed.count((*it)->data(0, kNameRole).toString()));

    QTreeWidgetItem* current = selected >= 0 ? nullptr : itemForNode(selectedName);
    if (selected >= 0)
        for (QTreeWidgetItemIterator it(this); *it; ++it)
            if ((*it)->data(0, kObjectRole).toInt() == selected)
                current = *it;
    setCurrentItem(current);
    applyFilter();
    rebuilding_ = false;
}

void HierarchyTree::selectObject(int object)
{
    rebuilding_ = true;
    QTreeWidgetItem* found = nullptr;
    for (QTreeWidgetItemIterator it(this); *it; ++it)
        if ((*it)->data(0, kObjectRole).toInt() == object && object >= 0)
            found = *it;
    setCurrentItem(found);
    if (found)
        scrollToItem(found);
    rebuilding_ = false;
}

void HierarchyTree::dropEvent(QDropEvent* e)
{
    QTreeWidgetItem* dragged = currentItem();
    if (!dragged) {
        e->ignore();
        return;
    }
    // Onto an item = child of it; above/below = sibling (same parent); empty space = root.
    QTreeWidgetItem* target = itemAt(e->position().toPoint());
    std::string parent;
    if (target) {
        if (dropIndicatorPosition() == QAbstractItemView::OnItem)
            parent = target->data(0, kNameRole).toString().toStdString();
        else if (target->parent())
            parent = target->parent()->data(0, kNameRole).toString().toStdString();
    }
    const std::string node = dragged->data(0, kNameRole).toString().toStdString();
    e->setDropAction(Qt::IgnoreAction);
    e->accept();
    if (engine::nodeParent(*scene_, node) == parent)
        return;
    if (!engine::setNodeParent(*scene_, node, parent)) {
        QMessageBox::information(this, tr("Hierarchy"), tr("An item cannot become a child of its own child."));
        return;
    }
    rebuild();
    emit hierarchyChanged();
}

void HierarchyTree::newGroup(const QString& parent)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New group"), tr("Name"), QLineEdit::Normal,
                                               qs(engine::uniqueNodeName(*scene_, "Group")), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (engine::nodeExists(*scene_, name.toStdString())) {
        QMessageBox::warning(this, tr("New group"), tr("The name \"%1\" is already used.").arg(name));
        return;
    }
    scene_->groups.push_back({name.toStdString(), parent.toStdString()});
    rebuild();
    if (auto* item = itemForNode(name))
        setCurrentItem(item);
    emit hierarchyChanged();
}

void HierarchyTree::contextMenuEvent(QContextMenuEvent* e)
{
    QTreeWidgetItem* item = itemAt(e->pos()); // viewport coordinates (QAbstractScrollArea forwards the event)
    const QString name = item ? item->data(0, kNameRole).toString() : QString();
    const bool isGroup = item && item->data(0, kObjectRole).toInt() < 0;
    QMenu menu(this);
    menu.addAction(tr("New group"), this, [this] { newGroup(QString()); });
    if (item) {
        menu.addAction(tr("New group inside \"%1\"").arg(name), this, [this, name] { newGroup(name); });
        menu.addAction(tr("Rename"), this, [this, item] { editItem(item, 0); });
        if (item->parent())
            menu.addAction(tr("Move to root"), this, [this, name] {
                engine::setNodeParent(*scene_, name.toStdString(), std::string());
                rebuild();
                emit hierarchyChanged();
            });
        if (isGroup)
            menu.addAction(tr("Remove group (keep its children)"), this, [this, name] {
                engine::removeGroup(*scene_, name.toStdString());
                rebuild();
                emit hierarchyChanged();
            });
        if (isGroup) {
            const int g = engine::findGroup(*scene_, name.toStdString());
            const bool instance = g >= 0 && scene_->groups[static_cast<std::size_t>(g)].prefabInstance == name.toStdString();
            menu.addSeparator();
            if (instance) {
                menu.addAction(tr("Prefab: revert to prefab"), this, [this, name] { emit prefabRequested(name, "revert"); });
                menu.addAction(tr("Prefab: apply changes to prefab"), this, [this, name] { emit prefabRequested(name, "apply"); });
                menu.addAction(tr("Prefab: unpack (normal objects)"), this, [this, name] { emit prefabRequested(name, "unpack"); });
                menu.addAction(tr("Prefab: open prefab"), this, [this, name] { emit prefabRequested(name, "open"); });
            } else if (g >= 0 && scene_->groups[static_cast<std::size_t>(g)].prefabInstance.empty()) {
                menu.addAction(tr("Save as prefab..."), this, [this, name] { emit prefabRequested(name, "save"); });
            }
        }
        menu.addSeparator();
        menu.addAction(tr("Expand all"), this, &QTreeWidget::expandAll);
        menu.addAction(tr("Collapse all"), this, &QTreeWidget::collapseAll);
    }
    menu.exec(e->globalPos());
}

// ---------------------------------------------------------------------------

DependencyPanel::DependencyPanel(engine::Scene* scene, QWidget* parent) : QWidget(parent), scene_(scene)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    title_ = new QLabel(this);
    title_->setWordWrap(true);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Item"), tr("Details")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(false);
    layout->addWidget(title_);
    layout->addWidget(tree_, 1);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const int object = item->data(0, Qt::UserRole + 1).toInt();
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty())
            emit assetActivated(path, item->data(0, Qt::UserRole + 2).toInt());
        else if (object >= 0)
            emit objectActivated(object);
    });
    showObject(-1);
}

void DependencyPanel::showObject(int object)
{
    tree_->clear();
    if (object < 0 || object >= scene_->objectCount()) {
        title_->setText(tr("Select an object to see what it uses and what uses it."));
        return;
    }
    const engine::Scene& scene = *scene_;
    title_->setText(tr("<b>%1</b>  ·  double-click opens the editor").arg(qs(engine::objectName(scene, object)).toHtmlEscaped()));

    auto objectItem = [&](QTreeWidgetItem* parent, int o, const QString& detail) {
        auto* it = new QTreeWidgetItem(parent, {qs(engine::objectName(scene, o)), detail});
        it->setData(0, Qt::UserRole + 1, o);
        it->setForeground(0, kindColor(scene, o));
        return it;
    };
    auto section = [&](const QString& text) {
        auto* it = new QTreeWidgetItem(tree_, {text});
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
        it->setData(0, Qt::UserRole + 1, -1);
        return it;
    };

    // Hierarchy relations.
    auto* family = section(tr("Hierarchy"));
    const std::string parent = engine::objectParent(scene, object);
    if (!parent.empty()) {
        const int p = engine::findObject(scene, parent);
        auto* it = p >= 0 ? objectItem(family, p, tr("parent")) : new QTreeWidgetItem(family, {qs(parent), tr("parent group")});
        if (p < 0)
            it->setData(0, Qt::UserRole + 1, -1);
    }
    const auto children = engine::descendantObjects(scene, engine::objectName(scene, object));
    for (int c : children)
        objectItem(family, c, engine::objectParent(scene, c) == engine::objectName(scene, object) ? tr("child") : tr("descendant"));
    if (family->childCount() == 0)
        new QTreeWidgetItem(family, {tr("(no parent or children)")});

    // Files used, each with the other objects that share it.
    auto* uses = section(tr("Uses"));
    QTreeWidgetItem* lastMaterial = nullptr;
    for (const engine::AssetUse& use : engine::objectDependencies(scene, object)) {
        QTreeWidgetItem* parentItem = use.depth > 0 && lastMaterial ? lastMaterial : uses;
        const QString label = QString("%1: %2").arg(tr(engine::assetKindLabel(use.kind)), qpath(use.path.filename()));
        auto* it = new QTreeWidgetItem(parentItem, {label, qs(use.detail)});
        it->setData(0, Qt::UserRole, qpath(use.path));
        it->setData(0, Qt::UserRole + 1, -1);
        it->setData(0, Qt::UserRole + 2, static_cast<int>(use.kind));
        it->setToolTip(0, qpath(use.path));
        if (!QFileInfo::exists(qpath(use.path)))
            it->setForeground(0, Qt::red);
        if (use.kind == engine::AssetKind::Material)
            lastMaterial = it;
        const auto users = use.depth == 0 ? engine::objectsUsing(scene, use.path) : std::vector<int>{};
        if (use.depth == 0 && users.size() > 1) { // textures inside a material share its users
            auto* shared = new QTreeWidgetItem(it, {tr("Also used by %n object(s)", nullptr, static_cast<int>(users.size()) - 1)});
            shared->setData(0, Qt::UserRole + 1, -1);
            for (int u : users)
                if (u != object)
                    objectItem(shared, u, tr("uses it too"));
        }
    }
    if (uses->childCount() == 0)
        new QTreeWidgetItem(uses, {tr("(no files: plain box)")});
    tree_->expandAll();
    // "Also used by" lists can be long; keep them folded.
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->text(0).startsWith(tr("Also used by").left(4)) && (*it)->childCount() > 3)
            (*it)->setExpanded(false);
}

// ---------------------------------------------------------------------------
void HierarchyTree::setFilter(const QString& text)
{
    filter_ = text.trimmed();
    applyFilter();
}

void HierarchyTree::applyFilter()
{
    const QStringList words = filter_.toLower().split(' ', Qt::SkipEmptyParts);
    // An item is shown when it matches or has a matching descendant.
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool childMatch = false;
        for (int i = 0; i < item->childCount(); ++i)
            childMatch = visit(item->child(i)) || childMatch;
        const QString text = (item->text(0) + ' ' + item->text(1)).toLower();
        bool match = true;
        for (const QString& w : words)
            match = match && text.contains(w);
        item->setHidden(!(match || childMatch));
        if (childMatch && !words.isEmpty())
            item->setExpanded(true);
        return match || childMatch;
    };
    for (int i = 0; i < topLevelItemCount(); ++i)
        visit(topLevelItem(i));
}

OutlinerPanel::OutlinerPanel(HierarchyTree* tree, QWidget* parent) : QWidget(parent), tree_(tree)
{
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search..."));
    search_->setClearButtonEnabled(true);
    addButton_ = new QToolButton(this);
    addButton_->setText(tr("Add"));
    addButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton_->setPopupMode(QToolButton::InstantPopup);
    count_ = new QLabel(this);
    count_->setObjectName("dim");
    auto* top = new QHBoxLayout();
    top->setContentsMargins(4, 4, 4, 2);
    top->addWidget(addButton_);
    top->addWidget(search_, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    tree_->setParent(this);
    layout->addWidget(tree_, 1);
    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(6, 3, 6, 3);
    bottom->addWidget(count_);
    layout->addLayout(bottom);
    connect(search_, &QLineEdit::textChanged, tree_, &HierarchyTree::setFilter);
}

void OutlinerPanel::setAddMenu(QMenu* menu, const QIcon& icon)
{
    addButton_->setMenu(menu);
    addButton_->setIcon(icon);
}

void OutlinerPanel::setCounts(int objects, int selected)
{
    count_->setText(tr("%n object(s)", nullptr, objects) + (selected > 0 ? tr(" (%1 selected)").arg(selected) : QString()));
}
