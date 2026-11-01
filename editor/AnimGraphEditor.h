#pragma once
#include <engine/AnimGraph.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;

// Character playing an animation graph on a small ground plane (drag to orbit, wheel to zoom).
// The graph is the editor's (shared) instance: edits show at once.
class AnimPreview : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit AnimPreview(QWidget* parent = nullptr);
    ~AnimPreview() override;
    bool setModel(const QString& path, QString* error);
    void setGraph(std::shared_ptr<engine::AnimGraph> graph);
    engine::Animator* animator(); // null without a model
    const engine::Skeleton* skeleton() const;
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    void setSpeed(float speed) { speed_ = speed; }
    void setStep(bool step); // a 20 cm step under the left foot (foot IK)
    void restart();          // back to the entry states, parameters keep their values
    void advance(float dt);  // one update now (scrubbing while paused)

signals:
    void ticked(); // after every update: the graph view and timeline follow the playback

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void wheelEvent(class QWheelEvent* e) override;

private:
    void cleanup();

    engine::Renderer renderer_;
    engine::Scene scene_;
    bool ready_ = false;
    bool paused_ = false, step_ = false;
    float speed_ = 1.0f;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 last_ = 0;
    float yaw_ = 0.5f, pitch_ = 0.2f, distance_ = 3.6f;
    QPoint dragFrom_;
};

// States of one layer as nodes, transitions as arrows. Drag nodes, double-click empty space
// for a new state, right-click for transitions, entry and delete.
class AnimGraphView : public QWidget {
    Q_OBJECT
public:
    explicit AnimGraphView(QWidget* parent = nullptr);
    void setGraph(engine::AnimGraph* graph, int layer);
    void setPlayback(const engine::AnimGraphState* state) { playback_ = state; update(); }
    int selectedState() const { return state_; }
    int selectedTransition() const { return transition_; }
    void select(int state, int transition);

signals:
    void selectionChanged();
    void edited(bool structure); // structure: states or transitions were added / removed

protected:
    void paintEvent(class QPaintEvent* e) override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void mouseReleaseEvent(class QMouseEvent* e) override;
    void mouseDoubleClickEvent(class QMouseEvent* e) override;
    void contextMenuEvent(class QContextMenuEvent* e) override;
    void resizeEvent(class QResizeEvent* e) override;
    void wheelEvent(class QWheelEvent* e) override;

private:
    void fit(); // centres the states (and the Any state node) in the view
    engine::AnimGraph::Layer* layer() const;
    QRectF nodeRect(int state) const; // graph space; -1 = the "Any state" node
    int stateAt(QPointF p) const;     // p in graph space; -2 = none
    int transitionAt(QPointF p) const;
    bool arrow(int transition, QPointF& from, QPointF& to) const;
    QPointF toGraph(QPointF p) const { return (p - pan_) / zoom_; }
    void addState(QPointF graphPos);
    void deleteState(int state);

    engine::AnimGraph* graph_ = nullptr;
    int layer_ = 0;
    const engine::AnimGraphState* playback_ = nullptr;
    int state_ = -2, transition_ = -1;
    int linkFrom_ = -2; // making a transition: source state, waiting for a click on the target
    QPointF mouse_;
    QPointF pan_{60.0, 60.0};
    double zoom_ = 1.0;
    bool dragging_ = false, panning_ = false, moved_ = false, fitPending_ = true;
    QPointF dragFrom_;
};

// Timeline of the clip the selected layer plays now: playhead, clip events (double-click adds,
// drag moves, right-click removes), click or drag scrubs.
class AnimTimeline : public QWidget {
    Q_OBJECT
public:
    explicit AnimTimeline(QWidget* parent = nullptr);
    void setClip(engine::AnimGraph* graph, const QString& clip, float duration, float time);

signals:
    void scrubbed(float time);
    void eventsEdited();

protected:
    void paintEvent(class QPaintEvent* e) override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void mouseReleaseEvent(class QMouseEvent* e) override;
    void mouseDoubleClickEvent(class QMouseEvent* e) override;

private:
    float timeAt(double x) const;
    double xAt(float time) const;
    int eventAt(QPointF p) const;

    engine::AnimGraph* graph_ = nullptr;
    QString clip_;
    float duration_ = 0.0f, time_ = 0.0f;
    int dragEvent_ = -1;
    bool scrubbing_ = false;
};

// Editor for .animgraph files: parameters, layers, states and transitions, IK chains, clip
// events, with a live character preview.
class AnimGraphEditor : public QWidget {
    Q_OBJECT
public:
    explicit AnimGraphEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error);
    bool revert(QString* error); // drops unsaved edits (the shared graph is reloaded from the file)
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();

private:
    void edited(bool structure = false); // graph changed: revision, modified, views
    void refreshLayers();
    void refreshParams();
    void refreshIk();
    void refreshDetails();
    void refreshClipLists();
    void tick();
    QStringList clipNames() const;
    QStringList boneNames() const;
    QStringList paramNames(bool floatsOnly) const;
    void renameParam(const std::string& from, const std::string& to);

    QString path_;
    std::shared_ptr<engine::AnimGraph> graph_;
    bool modified_ = false;
    bool updating_ = false;
    int layer_ = 0;

    AnimPreview* preview_ = nullptr;
    AnimGraphView* view_ = nullptr;
    AnimTimeline* timeline_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* layerCombo_ = nullptr;
    QComboBox* modelCombo_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QTableWidget* params_ = nullptr;

    // details
    QStackedWidget* details_ = nullptr;
    QLineEdit* layerName_ = nullptr;
    QComboBox* layerMask_ = nullptr;
    QDoubleSpinBox* layerWeight_ = nullptr;
    QComboBox* layerWeightParam_ = nullptr;
    QLineEdit* stateName_ = nullptr;
    QComboBox* stateKind_ = nullptr;
    QComboBox* stateParamX_ = nullptr;
    QComboBox* stateParamY_ = nullptr;
    QCheckBox* stateLoop_ = nullptr;
    QDoubleSpinBox* stateSpeed_ = nullptr;
    QTableWidget* samples_ = nullptr;
    QLabel* transitionLabel_ = nullptr;
    QDoubleSpinBox* transitionDuration_ = nullptr;
    QCheckBox* transitionExit_ = nullptr;
    QDoubleSpinBox* transitionExitTime_ = nullptr;
    QLineEdit* transitionConditions_ = nullptr;

    // IK
    QListWidget* ikList_ = nullptr;
    QComboBox* ikKind_ = nullptr;
    QLineEdit* ikName_ = nullptr;
    QComboBox* ikBones_[4] = {}; // root, mid, end, target
    QDoubleSpinBox* ikWeight_ = nullptr;
    QComboBox* ikWeightParam_ = nullptr;
    QComboBox* ikClip_ = nullptr;
    QDoubleSpinBox* ikTime_ = nullptr;
    QDoubleSpinBox* footMax_ = nullptr;
};
