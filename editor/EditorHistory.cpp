#include "EditorHistory.h"

SceneSnapshot MeshStore::capture(engine::Scene& scene, int selected)
{
    // Model meshes are shared lists (engine::PartList): copying the scene does not copy vertices.
    SceneSnapshot snap;
    snap.selected = selected;
    snap.scene = scene;
    return snap;
}

engine::Scene MeshStore::restore(const SceneSnapshot& snapshot) const
{
    return snapshot.scene;
}

SceneEditCommand::SceneEditCommand(const QString& label, std::shared_ptr<SceneSnapshot> before,
                                   std::shared_ptr<SceneSnapshot> after, Apply apply)
    : QUndoCommand(label), before_(std::move(before)), after_(std::move(after)), apply_(std::move(apply))
{
    age_.start();
}

void SceneEditCommand::undo()
{
    apply_(*before_);
}

void SceneEditCommand::redo()
{
    // The edit already happened when the command is pushed.
    if (first_) {
        first_ = false;
        return;
    }
    apply_(*after_);
}

bool SceneEditCommand::mergeWith(const QUndoCommand* other)
{
    const auto* o = static_cast<const SceneEditCommand*>(other);
    // Only repeated changes of the same object merge; adding or deleting never does.
    if (o->text() != text() || age_.elapsed() > 1000 || o->before_->selected != after_->selected ||
        o->after_->scene.objectCount() != after_->scene.objectCount() ||
        o->after_->scene.groups.size() != after_->scene.groups.size())
        return false;
    after_ = o->after_;
    age_.restart();
    return true;
}
