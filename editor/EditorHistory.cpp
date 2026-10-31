#include "EditorHistory.h"

SceneSnapshot MeshStore::capture(engine::Scene& scene, int selected)
{
    SceneSnapshot snap;
    snap.selected = selected;
    // Move the mesh parts out while copying so the copy stays cheap.
    std::vector<std::vector<engine::MeshPart>> parts(scene.models.size());
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        engine::Model& m = scene.models[i];
        const std::uint64_t key = m.parts.empty() ? 0 : m.parts.front().id;
        if (key && !meshes_.count(key))
            meshes_[key] = std::make_shared<const std::vector<engine::MeshPart>>(m.parts);
        snap.meshKeys.push_back(key);
        parts[i] = std::move(m.parts);
        m.parts.clear();
    }
    snap.scene = scene;
    for (std::size_t i = 0; i < scene.models.size(); ++i)
        scene.models[i].parts = std::move(parts[i]);
    return snap;
}

engine::Scene MeshStore::restore(const SceneSnapshot& snapshot) const
{
    engine::Scene scene = snapshot.scene;
    for (std::size_t i = 0; i < scene.models.size() && i < snapshot.meshKeys.size(); ++i)
        if (auto it = meshes_.find(snapshot.meshKeys[i]); it != meshes_.end())
            scene.models[i].parts = *it->second;
    return scene;
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
