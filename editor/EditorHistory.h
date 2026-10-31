#pragma once
#include <engine/Scene.h>

#include <QUndoCommand>
#include <QElapsedTimer>
#include <functional>
#include <map>
#include <memory>

// Full level state for undo/redo. Model meshes are not copied per step: copies
// of a model share its mesh parts (engine::PartList).
struct SceneSnapshot {
    engine::Scene scene;
    int selected = -1;
};

class MeshStore {
public:
    SceneSnapshot capture(engine::Scene& scene, int selected);
    engine::Scene restore(const SceneSnapshot& snapshot) const;
};

// One undoable edit: the level before and after. Consecutive edits with the
// same label within a second merge (dragging a spin box, moving with the gizmo).
class SceneEditCommand : public QUndoCommand {
public:
    using Apply = std::function<void(const SceneSnapshot&)>;
    SceneEditCommand(const QString& label, std::shared_ptr<SceneSnapshot> before, std::shared_ptr<SceneSnapshot> after,
                     Apply apply);
    void undo() override;
    void redo() override;
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    std::shared_ptr<SceneSnapshot> before_;
    std::shared_ptr<SceneSnapshot> after_;
    Apply apply_;
    bool first_ = true;
    QElapsedTimer age_;
};
