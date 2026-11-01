#pragma once
#include <engine/Scene.h>

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

// Entity I/O of the selected entity (engine/EntityIO.h): one row per connection
// "when I fire Output, send Input (Parameter) to Target after Delay [once]".
// Output and Input offer the known names but take any text (script outputs and
// on_input names). Below the table: the entities that send this one inputs.
class OutputsPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutputsPanel(QWidget* parent = nullptr);
    void setEntity(engine::Entity* entity, const engine::Scene* scene); // null = none
    void refresh();
    // The viewport click after "Pick target": sets the current row's target (a new row when none).
    void setPickedTarget(const QString& name);

signals:
    void edited();          // a connection changed (undo step, dirty, viewport refresh)
    void pickTargetRequested();
    void selectEntityRequested(const QString& name); // double click on an incoming connection

private:
    void rebuildRow(int row);
    void addConnection();
    void removeConnection();
    void updateIncoming();

    engine::Entity* entity_ = nullptr;
    const engine::Scene* scene_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* pick_ = nullptr;
    QLabel* incoming_ = nullptr;
    // Trigger volume (type "trigger"): who sets it off, whether only once; and for
    // switchable entities (triggers, relays, sounds, effects) whether they start on.
    QWidget* triggerRow_ = nullptr;
    QComboBox* filter_ = nullptr;
    QCheckBox* triggerOnce_ = nullptr;
    QCheckBox* startEnabled_ = nullptr;
    bool updating_ = false;
};
