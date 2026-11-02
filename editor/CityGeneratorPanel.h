#pragma once
#include <engine/CityGen.h>
#include <engine/Renderer.h>

#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;

// Tools > City generator (engine/CityGen.h, docs/CITY_GENERATOR.md): every setting of
// the generator and its kit, a live layout preview over the viewport, and Generate,
// which builds the town on worker threads and hands the objects to the level as one
// undo step. Settings are .city files, kits .citykit files (both text, in assets/city/).
class CityGeneratorPanel : public QWidget {
    Q_OBJECT
public:
    explicit CityGeneratorPanel(QWidget* parent = nullptr);
    ~CityGeneratorPanel() override;

    // Layout preview (street axes, lots, footprints, gateways, paths) in world space.
    const engine::DebugGeometry* preview() const { return &preview_; }
    bool previewShown() const;
    // Where "Use camera" puts the city's origin.
    void setCameraPosition(std::function<glm::vec3()> fn) { camera_ = std::move(fn); }
    // How many objects the level has below a group (asked before replacing a city).
    void setGroupSize(std::function<int(const std::string&)> fn) { groupSize_ = std::move(fn); }
    // Generate without asking (test hook FPS_EDITOR_CITY_GENERATE=1).
    void generateNow() { generate(false); }

signals:
    void previewChanged();                 // preview() or previewShown() changed
    void generated(std::shared_ptr<engine::city::CityObjects> objects, QString root, QString summary);
    void clearRequested(QString root);
    void message(QString text);

private:
    void buildUi();
    QWidget* layoutTab();
    QWidget* rulesTab();
    QWidget* kitTab();
    void rebuildGrid();
    void refreshWidgets();      // settings_ / kit_ -> widgets
    void refreshKitTables();
    void changed();             // a setting changed: preview again soon
    void computePreview();
    void loadCity(const QString& file);
    bool saveCity(const QString& file);
    void loadKitFile(const QString& file);
    void generate(bool confirm = true);
    void measure(int building, const QString& file, bool append);
    QString kitDir() const;

    engine::city::Settings settings_;
    engine::city::Kit kit_;
    QString cityFile_;
    engine::DebugGeometry preview_;
    std::function<glm::vec3()> camera_;
    std::function<int(const std::string&)> groupSize_;
    std::vector<std::function<void()>> refreshers_; // update the plain fields from settings_
    std::shared_ptr<int> previewGen_ = std::make_shared<int>(0);
    bool updating_ = false;
    bool busy_ = false;

    QLabel* fileLabel_ = nullptr;
    QLineEdit* columns_ = nullptr;
    QLineEdit* rows_ = nullptr;
    QTableWidget* blocks_ = nullptr;
    QTableWidget* streetsX_ = nullptr;
    QTableWidget* streetsZ_ = nullptr;
    QTableWidget* buildings_ = nullptr;
    QTableWidget* props_ = nullptr;
    QLineEdit* kitPath_ = nullptr;
    QLineEdit* materials_[6] = {};
    QCheckBox* showPreview_ = nullptr;
    QPushButton* generate_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* previewTimer_ = nullptr;
};
