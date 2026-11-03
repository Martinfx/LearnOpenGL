#include "MapPanel.h"

#include <engine/JobSystem.h>
#include <engine/Log.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <chrono>

namespace {
QDoubleSpinBox* spin(QWidget* parent, double lo, double hi, double step, int decimals, const QString& suffix, const QString& tip)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setToolTip(tip);
    s->setKeyboardTracking(false);
    return s;
}

QColor toQColor(glm::vec3 c) { return QColor::fromRgbF(c.r, c.g, c.b); }

void paintSwatch(QPushButton* b, glm::vec3 c)
{
    b->setStyleSheet(QStringLiteral("background-color: %1; min-height: 18px;").arg(toQColor(c).name()));
}
} // namespace

MapPanel::MapPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    sizeZ_ = spin(this, 0.0, 100000.0, 10.0, 0, tr(" m"), tr("Size of the map along Z (north-south), 0 = the same as along X (square)"));
    form->addRow(tr("Map size Z"), sizeZ_);

    auto* bakeHead = new QLabel(tr("<i>Render from above</i>"), this);
    form->addRow(bakeHead);
    ppm_ = spin(this, 0.5, 24.0, 0.5, 1, tr(" px/m"), tr("Detail of the picture; it is kept between 2048 and 4096 pixels"));
    daylight_ = new QCheckBox(tr("Sunny day (readable shadows)"), this);
    daylight_->setToolTip(tr("On: the level's colours under a sun from the angle below. Off: the level's own light and weather."));
    azimuth_ = spin(this, 0.0, 360.0, 15.0, 0, tr(" °"), tr("Where the sun is, degrees from north toward east (315 = north-west, as on paper maps)"));
    elevation_ = spin(this, 5.0, 89.0, 5.0, 0, tr(" °"), tr("Sun above the horizon: lower = longer shadows"));
    exposure_ = spin(this, 0.1, 4.0, 0.05, 2, QString(), tr("Brightness of the render (the picture is also levelled automatically)"));
    saturation_ = spin(this, 0.0, 2.0, 0.05, 2, QString(), tr("Colour: 0 grey, 1 as rendered"));
    contrast_ = spin(this, 0.2, 2.0, 0.05, 2, QString(), tr("Contrast of the picture"));
    outlines_ = spin(this, 0.0, 1.0, 0.05, 2, QString(), tr("Dark lines along height steps: house walls, ruins, kerbs"));
    heightShade_ = spin(this, 0.0, 1.0, 0.05, 2, QString(), tr("High roofs lighter, low ground darker"));
    form->addRow(tr("Detail"), ppm_);
    form->addRow(QString(), daylight_);
    form->addRow(tr("Sun direction"), azimuth_);
    form->addRow(tr("Sun height"), elevation_);
    form->addRow(tr("Exposure"), exposure_);
    form->addRow(tr("Saturation"), saturation_);
    form->addRow(tr("Contrast"), contrast_);
    form->addRow(tr("Outlines"), outlines_);
    form->addRow(tr("Height shading"), heightShade_);

    form->addRow(new QLabel(tr("<i>PDA screen</i>"), this));
    tint_ = new QPushButton(this);
    tint_->setToolTip(tr("Colour multiplied over the map (a worn, greenish PDA screen)"));
    scanlines_ = spin(this, 0.0, 1.0, 0.05, 2, QString(), tr("Thin dark lines of the screen"));
    grid_ = spin(this, 0.0, 1000.0, 10.0, 0, tr(" m"), tr("Grid with sector letters and numbers, 0 = none"));
    radius_ = spin(this, 10.0, 1000.0, 10.0, 0, tr(" m"), tr("Minimap: metres from its middle to the edge"));
    maxZoom_ = spin(this, 1.0, 64.0, 1.0, 0, tr(" x"), tr("Full map (M): closest zoom, times the whole map"));
    anomalies_ = new QCheckBox(tr("Show anomaly fields"), this);
    form->addRow(tr("Screen tint"), tint_);
    form->addRow(tr("Scan lines"), scanlines_);
    form->addRow(tr("Grid"), grid_);
    form->addRow(tr("Minimap range"), radius_);
    form->addRow(tr("Max zoom"), maxZoom_);
    form->addRow(QString(), anomalies_);

    auto* buttons = new QWidget(this);
    auto* row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    bakeButton_ = new QPushButton(tr("Bake map"), buttons);
    bakeButton_->setToolTip(tr("Renders the level from above now and stores it where the game finds it (the game bakes it itself on the "
                               "first load of a changed level)"));
    exportButton_ = new QPushButton(tr("Export PNG..."), buttons);
    exportButton_->setEnabled(false);
    row->addWidget(bakeButton_, 1);
    row->addWidget(exportButton_);
    form->addRow(buttons);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setVisible(false);
    form->addRow(progress_);
    status_ = new QLabel(this);
    status_->setObjectName("dim");
    status_->setWordWrap(true);
    form->addRow(status_);
    preview_ = new QLabel(this);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(0);
    form->addRow(preview_);

    timer_ = new QTimer(this);
    timer_->setInterval(0);
    connect(timer_, &QTimer::timeout, this, &MapPanel::tick);
    connect(bakeButton_, &QPushButton::clicked, this, &MapPanel::bake);
    connect(exportButton_, &QPushButton::clicked, this, [this] {
        if (!last_)
            return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export map"), QString(), tr("PNG image (*.png)"));
        if (path.isEmpty())
            return;
        QImage img(last_->rgba.data(), last_->width, last_->height, last_->width * 4, QImage::Format_RGBA8888);
        status_->setText(img.save(path) ? tr("Saved %1").arg(path) : tr("Cannot write %1").arg(path));
    });
    connect(tint_, &QPushButton::clicked, this, [this] {
        if (!scene_)
            return;
        const QColor c = QColorDialog::getColor(toQColor(scene_->map.tint), this, tr("PDA screen tint"));
        if (!c.isValid())
            return;
        scene_->map.tint = glm::vec3(c.redF(), c.greenF(), c.blueF());
        paintSwatch(tint_, scene_->map.tint);
        emit edited();
    });
    for (QDoubleSpinBox* s : {sizeZ_, ppm_, azimuth_, elevation_, exposure_, saturation_, contrast_, outlines_, heightShade_, scanlines_, grid_, radius_, maxZoom_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MapPanel::apply);
    for (QCheckBox* c : {daylight_, anomalies_})
        connect(c, &QCheckBox::toggled, this, &MapPanel::apply);
}

void MapPanel::setScene(engine::Scene* scene)
{
    scene_ = scene;
    refresh();
}

void MapPanel::refresh()
{
    if (!scene_)
        return;
    loading_ = true;
    const engine::MapSettings& m = scene_->map;
    sizeZ_->setValue(m.sizeZ);
    ppm_->setValue(m.pixelsPerMetre);
    daylight_->setChecked(m.daylight);
    azimuth_->setValue(m.sunAzimuth);
    elevation_->setValue(m.sunElevation);
    exposure_->setValue(m.exposure);
    saturation_->setValue(m.saturation);
    contrast_->setValue(m.contrast);
    outlines_->setValue(m.outlines);
    heightShade_->setValue(m.heightShade);
    paintSwatch(tint_, m.tint);
    scanlines_->setValue(m.scanlines);
    grid_->setValue(m.grid);
    radius_->setValue(m.minimapRadius);
    maxZoom_->setValue(m.maxZoom);
    anomalies_->setChecked(m.showAnomalies);
    azimuth_->setEnabled(m.daylight);
    elevation_->setEnabled(m.daylight);
    loading_ = false;
}

void MapPanel::apply()
{
    if (!scene_ || loading_)
        return;
    engine::MapSettings& m = scene_->map;
    m.sizeZ = static_cast<float>(sizeZ_->value());
    m.pixelsPerMetre = static_cast<float>(ppm_->value());
    m.daylight = daylight_->isChecked();
    m.sunAzimuth = static_cast<float>(azimuth_->value());
    m.sunElevation = static_cast<float>(elevation_->value());
    m.exposure = static_cast<float>(exposure_->value());
    m.saturation = static_cast<float>(saturation_->value());
    m.contrast = static_cast<float>(contrast_->value());
    m.outlines = static_cast<float>(outlines_->value());
    m.heightShade = static_cast<float>(heightShade_->value());
    m.scanlines = static_cast<float>(scanlines_->value());
    m.grid = static_cast<float>(grid_->value());
    m.minimapRadius = static_cast<float>(radius_->value());
    m.maxZoom = static_cast<float>(maxZoom_->value());
    m.showAnomalies = anomalies_->isChecked();
    azimuth_->setEnabled(m.daylight);
    elevation_->setEnabled(m.daylight);
    emit edited();
}

void MapPanel::bake()
{
    if (!scene_ || !bakeStep)
        return;
    if (finishing_.valid())
        return; // compressing: done in a moment
    if (baker_) { // a second click cancels
        timer_->stop();
        baker_.reset();
        progress_->setVisible(false);
        bakeButton_->setText(tr("Bake map"));
        status_->setText(tr("Cancelled."));
        return;
    }
    baker_ = std::make_unique<engine::MapBaker>();
    baker_->begin(*scene_);
    const QString level = levelFile ? levelFile() : QString();
    cacheFile_ = engine::mapCachePath(std::filesystem::path(level.isEmpty() ? QStringLiteral("untitled.level").toStdU16String() : level.toStdU16String()),
                                      *scene_);
    progress_->setValue(0);
    progress_->setVisible(true);
    bakeButton_->setText(tr("Cancel"));
    status_->setText(tr("Rendering %1 x %2 m from above...").arg(baker_->area().size.x, 0, 'f', 0).arg(baker_->area().size.y, 0, 'f', 0));
    timer_->start();
}

void MapPanel::tick()
{
    if (baker_ && !baker_->done()) {
        if (!bakeStep(*baker_, 60.0)) { // no GL context yet
            timer_->stop();
            baker_.reset();
            progress_->setVisible(false);
            bakeButton_->setText(tr("Bake map"));
            status_->setText(tr("The viewport is not ready."));
            return;
        }
        progress_->setValue(static_cast<int>(baker_->progress() * 90.0f));
        return;
    }
    if (baker_ && !finishing_.valid()) {
        // Grading, outlines, compression and the cache file on a worker.
        engine::MapBaker* baker = baker_.get();
        const std::filesystem::path file = cacheFile_;
        finishing_ = engine::JobSystem::global().submit([baker, file] {
            engine::MapImage image = baker->finish();
            engine::writeMapCache(file, engine::cookMapImage(image), image.area);
            return image;
        });
        progress_->setValue(92);
        return;
    }
    if (finishing_.valid() && finishing_.wait_for(std::chrono::milliseconds(5)) == std::future_status::ready) {
        timer_->stop();
        const engine::MapImage image = finishing_.get();
        baker_.reset();
        progress_->setVisible(false);
        bakeButton_->setText(tr("Bake map"));
        showImage(image);
        LOG_INFO("map", "map baked in the editor: %d x %d px, %s", image.width, image.height, cacheFile_.string().c_str());
        status_->setText(tr("%1 x %2 px, %3 x %4 m. The game uses it while the level stays as it is.")
                             .arg(image.width)
                             .arg(image.height)
                             .arg(image.area.size.x, 0, 'f', 0)
                             .arg(image.area.size.y, 0, 'f', 0));
    } else if (!baker_ && !finishing_.valid()) {
        timer_->stop();
    }
}

void MapPanel::showImage(const engine::MapImage& image)
{
    last_ = std::make_shared<engine::MapImage>(image);
    const QImage img(last_->rgba.data(), last_->width, last_->height, last_->width * 4, QImage::Format_RGBA8888);
    const int w = std::max(160, width() - 8);
    preview_->setPixmap(QPixmap::fromImage(img.scaled(w, w * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    exportButton_->setEnabled(true);
}
