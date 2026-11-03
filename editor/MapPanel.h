#pragma once
#include <engine/MapBake.h>
#include <engine/Scene.h>

#include <QWidget>
#include <functional>
#include <future>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

// Level > Map: how the in-game map (PDA and minimap) looks. The picture is the level
// rendered from above (engine/MapBake.h): light, detail and styling of the bake, the
// PDA screen look, and Bake (renders it now in the viewport's GL context, a few tiles
// per tick so the editor stays responsive; the result is previewed here and written to
// the cook cache, where the game finds it when the level is unchanged).
class MapPanel : public QWidget {
    Q_OBJECT
public:
    explicit MapPanel(QWidget* parent = nullptr);
    void setScene(engine::Scene* scene); // refreshes the fields
    void refresh();
    void bakeNow() { bake(); } // test hook FPS_EDITOR_MAP_BAKE=1

    // Set by the main window: one bake step in the viewport's GL context (false = no GL yet),
    // and the level file (for the cache name).
    std::function<bool(engine::MapBaker& baker, double budgetMs)> bakeStep;
    std::function<QString()> levelFile;

signals:
    void edited(); // a value changed (dirty)

private:
    void apply();
    void bake();
    void tick();
    void showImage(const engine::MapImage& image);

    engine::Scene* scene_ = nullptr;
    bool loading_ = false;
    QDoubleSpinBox* sizeZ_ = nullptr;
    QDoubleSpinBox* ppm_ = nullptr;
    QCheckBox* daylight_ = nullptr;
    QDoubleSpinBox* azimuth_ = nullptr;
    QDoubleSpinBox* elevation_ = nullptr;
    QDoubleSpinBox* exposure_ = nullptr;
    QDoubleSpinBox* saturation_ = nullptr;
    QDoubleSpinBox* contrast_ = nullptr;
    QDoubleSpinBox* outlines_ = nullptr;
    QDoubleSpinBox* heightShade_ = nullptr;
    QPushButton* tint_ = nullptr;
    QDoubleSpinBox* scanlines_ = nullptr;
    QDoubleSpinBox* grid_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QDoubleSpinBox* maxZoom_ = nullptr;
    QCheckBox* anomalies_ = nullptr;
    QPushButton* bakeButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;
    std::unique_ptr<engine::MapBaker> baker_;
    std::filesystem::path cacheFile_;
    std::future<engine::MapImage> finishing_;
    std::shared_ptr<engine::MapImage> last_; // for Export
};
