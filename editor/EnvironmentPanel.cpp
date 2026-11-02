#include "EnvironmentPanel.h"
#include <engine/Audio.h>
#include "SoundEditor.h"
#include "Vec3Editor.h"
#include "AssetPaths.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QMessageBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace {

QColor toQColor(glm::vec3 c)
{
    return QColor::fromRgbF(glm::clamp(c.r, 0.0f, 1.0f), glm::clamp(c.g, 0.0f, 1.0f), glm::clamp(c.b, 0.0f, 1.0f));
}

void paintButton(QPushButton* button, glm::vec3 c)
{
    const QColor q = toQColor(c);
    button->setText(q.name());
    button->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px solid #555; padding: 3px; }")
                              .arg(q.name(), q.lightnessF() > 0.55 ? "black" : "white"));
}

struct Preset {
    const char* name;
    glm::vec3 sun;
    engine::Environment env;
};

std::vector<Preset> presets()
{
    std::vector<Preset> list;
    engine::Environment clear; // engine defaults: clear day
    list.push_back({"Clear day", {-0.4f, -1.0f, -0.3f}, clear});

    engine::Environment zone;
    zone.sunColor = {1.0f, 0.86f, 0.68f};
    zone.sunIntensity = 2.4f;
    zone.skyZenith = {0.38f, 0.42f, 0.44f};
    zone.skyHorizon = {0.68f, 0.66f, 0.58f};
    zone.groundColor = {0.26f, 0.24f, 0.2f};
    zone.cloudCover = 0.72f;
    zone.ambientIntensity = 0.8f;
    zone.fogColor = {0.6f, 0.6f, 0.54f};
    zone.fogDensity = 0.018f;
    zone.fogHeight = 1.0f;
    zone.fogHeightFalloff = 0.12f;
    zone.fogSunScatter = 1.2f;
    zone.bloomThreshold = 1.3f;
    zone.bloomIntensity = 0.05f;
    zone.ssaoIntensity = 1.1f;
    zone.saturation = 0.72f;
    zone.contrast = 1.1f;
    zone.tint = {1.02f, 1.0f, 0.9f};
    zone.vignette = 0.35f;
    zone.grain = 0.035f;
    list.push_back({"Overcast zone (post-apo)", {-0.45f, -0.75f, -0.3f}, zone});

    engine::Environment dusk = zone;
    dusk.sunColor = {1.0f, 0.55f, 0.3f};
    dusk.sunIntensity = 2.0f;
    dusk.skyZenith = {0.22f, 0.26f, 0.36f};
    dusk.skyHorizon = {0.85f, 0.55f, 0.35f};
    dusk.cloudCover = 0.5f;
    dusk.ambientIntensity = 0.55f;
    dusk.fogColor = {0.55f, 0.42f, 0.35f};
    dusk.fogDensity = 0.014f;
    dusk.fogSunScatter = 2.0f;
    dusk.saturation = 0.85f;
    dusk.tint = {1.05f, 0.95f, 0.88f};
    list.push_back({"Dusk", {-0.85f, -0.18f, -0.2f}, dusk});

    engine::Environment haze = zone;
    haze.sunColor = {0.9f, 1.0f, 0.7f};
    haze.sunIntensity = 1.6f;
    haze.skyZenith = {0.42f, 0.46f, 0.36f};
    haze.skyHorizon = {0.66f, 0.7f, 0.5f};
    haze.cloudCover = 0.85f;
    haze.fogColor = {0.55f, 0.6f, 0.4f};
    haze.fogDensity = 0.04f;
    haze.fogHeight = 2.0f;
    haze.saturation = 0.65f;
    haze.tint = {0.95f, 1.03f, 0.85f};
    haze.vignette = 0.45f;
    list.push_back({"Toxic haze", {-0.3f, -0.9f, -0.2f}, haze});

    engine::Environment night = zone;
    night.sunColor = {0.55f, 0.65f, 0.9f}; // moonlight
    night.sunIntensity = 0.35f;
    night.skyZenith = {0.02f, 0.03f, 0.05f};
    night.skyHorizon = {0.07f, 0.08f, 0.1f};
    night.groundColor = {0.03f, 0.03f, 0.03f};
    night.cloudCover = 0.6f;
    night.ambientIntensity = 0.35f;
    night.fogColor = {0.06f, 0.07f, 0.09f};
    night.fogDensity = 0.03f;
    night.exposure = 1.6f;
    night.saturation = 0.5f;
    night.tint = {0.9f, 0.95f, 1.1f};
    night.grain = 0.06f;
    list.push_back({"Night", {-0.3f, -0.8f, 0.4f}, night});
    return list;
}

} // namespace

EnvironmentPanel::EnvironmentPanel(engine::Scene* scene, QWidget* parent)
    : QWidget(parent), scene_(scene)
{
    using E = engine::Environment;
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    preset_ = new QComboBox(this);
    preset_->addItem(tr("Apply preset..."));
    for (const Preset& p : presets())
        preset_->addItem(tr(p.name));
    form->addRow(tr("Preset"), preset_);
    connect(preset_, &QComboBox::activated, this, [this](int index) {
        if (index > 0)
            applyPreset(index - 1);
        preset_->setCurrentIndex(0);
    });

    auto section = [&](const QString& title) { form->addRow(new QLabel("<b>" + title + "</b>", this)); };
    section(tr("Sun"));
    sun_ = new Vec3Editor(-1.0, 1.0, 0.05, this);
    sun_->setToolTip(tr("Direction the sunlight travels (pointing down = noon)"));
    form->addRow(tr("Direction"), sun_);
    connect(sun_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (updating_ || glm::length(v) < 1e-3f)
            return;
        scene_->sunDirection = v;
        changed();
    });
    addColor(form, tr("Color"), &E::sunColor);
    addFloat(form, tr("Intensity"), &E::sunIntensity, 0, 20, 0.1, 2);
    section(tr("Time of day"));
    buildTimeOfDay(form);
    section(tr("Sky"));
    addColor(form, tr("Zenith"), &E::skyZenith);
    addColor(form, tr("Horizon"), &E::skyHorizon);
    addColor(form, tr("Ground"), &E::groundColor);
    addFloat(form, tr("Clouds"), &E::cloudCover, 0, 1, 0.05, 2, tr("0 clear, 1 overcast"));
    addFloat(form, tr("Sky light"), &E::ambientIntensity, 0, 5, 0.05, 2, tr("Ambient light and reflections from the sky"));
    addFloat(form, tr("Physical atmosphere"), &E::atmosphere, 0, 1, 0.05, 2,
             tr("0 = the colours above, 1 = sky, horizon, fog and sunlight colour from a physical model of the air (Rayleigh, Mie, ozone)"));
    addFloat(form, tr("Haze"), &E::atmosphereHaze, 0, 10, 0.1, 2, tr("Dust and humidity in the air: 0.3 crisp, 1 normal, 5 smog (physical atmosphere)"));
    addFloat(form, tr("Sky brightness"), &E::atmosphereBrightness, 0, 5, 0.05, 2, tr("Multiplier of the physical sky's light"));
    addFloat(form, tr("Sky occlusion"), &E::skyOcclusion, 0, 1, 0.05, 2,
             tr("Buildings and roofs within ~40 m hide the sky: darker street bottoms and rooms (sky light and reflections)"));
    section(tr("Fog"));
    addColor(form, tr("Color"), &E::fogColor);
    addFloat(form, tr("Density"), &E::fogDensity, 0, 0.5, 0.002, 3);
    addFloat(form, tr("Height"), &E::fogHeight, -100, 100, 0.5, 1, tr("Fog is densest below this height (m)"));
    addFloat(form, tr("Height falloff"), &E::fogHeightFalloff, 0, 2, 0.01, 3);
    addFloat(form, tr("Sun glow"), &E::fogSunScatter, 0, 5, 0.1, 2, tr("Fog brightens toward the sun"));
    addFloat(form, tr("Volumetric range"), &E::fogVolumetricRange, 0, 256, 4, 0,
             tr("Meters of volumetric fog in front of the camera (light shafts, lamps), 0 = simple fog only"));
    addFloat(form, tr("Light shafts"), &E::fogLightShafts, 0, 5, 0.05, 2, tr("Strength of the sun in the fog: rays through gaps and shadows"));
    addFloat(form, tr("Anisotropy"), &E::fogAnisotropy, -0.9, 0.9, 0.05, 2, tr("0 even scattering, higher = brighter looking toward the light"));
    addFloat(form, tr("Noise"), &E::fogNoise, 0, 1, 0.05, 2, tr("0 smooth, 1 patchy fog drifting with the wind"));
    addFloat(form, tr("Local lights"), &E::fogLocalLights, 0, 5, 0.05, 2, tr("How strongly lamps and the flashlight light up the fog"));
    section(tr("Camera and image"));
    addFloat(form, tr("Exposure"), &E::exposure, 0.05, 10, 0.05, 2, tr("Manual exposure; with auto-exposure it is the compensation"));
    addFloat(form, tr("Auto exposure"), &E::autoExposure, 0, 1, 0.05, 2,
             tr("0 manual, 1 the camera adapts to the brightness of the view like an eye"));
    addFloat(form, tr("Auto min EV"), &E::exposureMinEv, -10, 0, 0.25, 2, tr("Darkest correction in stops (bright scenes)"));
    addFloat(form, tr("Auto max EV"), &E::exposureMaxEv, 0, 10, 0.25, 2,
             tr("Brightest correction in stops: keep it low so nights and interiors stay dark"));
    addFloat(form, tr("Adapt to dark"), &E::adaptBrighten, 0.05, 20, 0.1, 2, tr("Speed (1/s) when the view gets darker"));
    addFloat(form, tr("Adapt to light"), &E::adaptDarken, 0.05, 20, 0.1, 2, tr("Speed (1/s) when the view gets brighter"));
    addFloat(form, tr("Bloom"), &E::bloomIntensity, 0, 1, 0.01, 3);
    addFloat(form, tr("Bloom threshold"), &E::bloomThreshold, 0, 10, 0.1, 2);
    addFloat(form, tr("AO radius"), &E::ssaoRadius, 0.05, 5, 0.05, 2, tr("Ambient occlusion radius (m)"));
    addFloat(form, tr("AO strength"), &E::ssaoIntensity, 0, 4, 0.05, 2);
    addFloat(form, tr("Reflections (SSR)"), &E::ssrIntensity, 0, 1, 0.05, 2,
             tr("Screen-space reflections on smooth and wet surfaces (needs the SSR quality option)"));
    addFloat(form, tr("Shadow distance"), &E::shadowDistance, 5, 500, 5, 0, tr("Meters covered by sun shadows"));
    addFloat(form, tr("Saturation"), &E::saturation, 0, 2, 0.02, 2);
    addFloat(form, tr("Contrast"), &E::contrast, 0.5, 2, 0.02, 2);
    addColor(form, tr("Tint"), &E::tint);
    addFloat(form, tr("Temperature"), &E::temperature, -1, 1, 0.02, 2, tr("White balance: -1 cold (blue) .. 1 warm (orange)"));
    addFloat(form, tr("Green / magenta"), &E::greenMagenta, -1, 1, 0.02, 2, tr("White balance tint"));
    addVec3(form, tr("Lift"), &E::lift, -0.5, 0.5, 0.01, tr("Shadows: added to dark tones (R, G, B)"));
    addVec3(form, tr("Gamma"), &E::gamma, 0.2, 3.0, 0.02, tr("Midtones: above 1 brighter (R, G, B)"));
    addVec3(form, tr("Gain"), &E::gain, 0.0, 3.0, 0.02, tr("Highlights multiplier (R, G, B)"));
    {
        // Color LUT: a strip image (N*N x N) applied last; export the neutral one, grade it in Krita/GIMP.
        lutEdit_ = new QLineEdit(this);
        lutEdit_->setReadOnly(true);
        lutEdit_->setPlaceholderText(tr("none"));
        auto* browse = new QPushButton(tr("..."), this);
        browse->setMaximumWidth(32);
        auto* clear = new QPushButton(tr("x"), this);
        clear->setMaximumWidth(28);
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(lutEdit_, 1);
        rowLayout->addWidget(browse);
        rowLayout->addWidget(clear);
        form->addRow(tr("Color LUT"), row);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, tr("Color LUT"), assetPath("textures/luts"),
                                                              tr("LUT strips (*.png *.jpg)"));
            if (path.isEmpty())
                return;
            scene_->environment.lut = std::filesystem::path(path.toStdU16String());
            refresh();
            changed();
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            scene_->environment.lut.clear();
            refresh();
            changed();
        });
        addFloat(form, tr("LUT strength"), &E::lutStrength, 0, 1, 0.05, 2);
        auto* exportLut = new QPushButton(tr("Export neutral LUT..."), this);
        exportLut->setToolTip(tr("Saves an unchanged 32x32x32 LUT strip: grade a screenshot with it in an image editor, "
                                 "apply the same edits to the strip and pick it as Color LUT"));
        form->addRow(QString(), exportLut);
        connect(exportLut, &QPushButton::clicked, this, &EnvironmentPanel::exportNeutralLut);
    }
    addFloat(form, tr("Vignette"), &E::vignette, 0, 1, 0.02, 2);
    addFloat(form, tr("Film grain"), &E::grain, 0, 0.3, 0.005, 3);
    section(tr("Weather"));
    addFloat(form, tr("Rain"), &E::rain, 0, 1, 0.05, 2, tr("0 dry, 1 downpour: rain sound volume"));
    addFloat(form, tr("Wind"), &E::wind, 0, 1, 0.05, 2,
             tr("0 calm, 1 storm (~20 m/s): wind sound, vegetation sway (Wind sway of models and foliage) and particles with Level wind"));
    addFloat(form, tr("Wind direction"), &E::windDirection, -360, 360, 5, 0, tr("Degrees the wind blows toward: 0 = +X, 90 = +Z"));
    addFloat(form, tr("Gusts"), &E::windGusts, 0, 1, 0.05, 2, tr("0 steady wind .. 1 strong gusts rolling across the level"));
    addFloat(form, tr("Storm"), &E::storm, 0, 1, 0.05, 2,
             tr("Lightning: 0 none, 1 a strike every few seconds (flash of the sky and light, thunder delayed by distance)"));
    addFloat(form, tr("Puddles"), &E::puddles, 0, 1, 0.05, 2, tr("How much water collects on flat ground open to the sky when wet"));
    rainSound_ = new SoundPicker(this);
    rainSound_->setToolTip(tr("Rain loop; empty = sounds/ambient/rain.sound"));
    form->addRow(tr("Rain sound"), rainSound_);
    windSound_ = new SoundPicker(this);
    windSound_->setToolTip(tr("Wind loop; empty = sounds/ambient/wind_storm.sound"));
    form->addRow(tr("Wind sound"), windSound_);
    thunderSound_ = new SoundPicker(this);
    thunderSound_->setToolTip(tr("Thunder (variations); empty = sounds/ambient/thunder.sound"));
    form->addRow(tr("Thunder sound"), thunderSound_);
    auto soundEdited = [this](std::filesystem::path engine::Environment::*member, const QString& path) {
        if (updating_)
            return;
        scene_->environment.*member = std::filesystem::path(path.toStdU16String());
        changed();
    };
    connect(rainSound_, &SoundPicker::pathChanged, this,
            [soundEdited](const QString& p) { soundEdited(&engine::Environment::rainSound, p); });
    connect(windSound_, &SoundPicker::pathChanged, this,
            [soundEdited](const QString& p) { soundEdited(&engine::Environment::windSound, p); });
    connect(thunderSound_, &SoundPicker::pathChanged, this,
            [soundEdited](const QString& p) { soundEdited(&engine::Environment::thunderSound, p); });
    section(tr("Weather cycle"));
    buildWeatherCycle(form);
    section(tr("Anomaly storm"));
    buildAnomalyStorm(form);
    section(tr("Sound"));
    reverb_ = new QComboBox(this);
    for (const std::string& name : engine::reverbPresetNames())
        reverb_->addItem(QString::fromStdString(name));
    reverb_->setToolTip(tr("Room reverb of the whole level; reverb zones (Add > Reverb zone) override it where the player is"));
    form->addRow(tr("Reverb"), reverb_);
    connect(reverb_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (updating_)
            return;
        scene_->environment.reverb = name.toStdString();
        changed();
    });
    addFloat(form, tr("Reverb amount"), &E::reverbAmount, 0, 1, 0.05, 2, tr("How strongly the reverb is mixed in"));
    ambience_ = new SoundPicker(this);
    ambience_->setFileKind(tr("Ambience sets (*.ambience)"), QStringLiteral("sounds/ambience"), false);
    ambience_->setToolTip(tr("Ambience set of the level: looping layers and random far sounds (edit .ambience files in the Content "
                             "Browser); Ambience zones replace it where the player is"));
    form->addRow(tr("Ambience"), ambience_);
    connect(ambience_, &SoundPicker::pathChanged, this,
            [soundEdited](const QString& p) { soundEdited(&engine::Environment::ambience, p); });
    layout->addStretch(1);
    refresh();
}

QDoubleSpinBox* EnvironmentPanel::addFloat(QFormLayout* form, const QString& label, float engine::Environment::*member,
                                           double min, double max, double step, int decimals, const QString& tip)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setToolTip(tip);
    spin->setKeyboardTracking(false);
    form->addRow(label, spin);
    floats_.push_back({spin, member});
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double v) {
        if (updating_)
            return;
        scene_->environment.*member = static_cast<float>(v);
        changed();
    });
    return spin;
}

void EnvironmentPanel::addColor(QFormLayout* form, const QString& label, glm::vec3 engine::Environment::*member)
{
    auto* button = new QPushButton(this);
    form->addRow(label, button);
    colors_.push_back({button, member});
    connect(button, &QPushButton::clicked, this, [this, button, member, label] {
        const QColor picked = QColorDialog::getColor(toQColor(scene_->environment.*member), this, label);
        if (!picked.isValid())
            return;
        const glm::vec3 c(static_cast<float>(picked.redF()), static_cast<float>(picked.greenF()),
                          static_cast<float>(picked.blueF()));
        scene_->environment.*member = c;
        paintButton(button, c);
        changed();
    });
}

void EnvironmentPanel::addVec3(QFormLayout* form, const QString& label, glm::vec3 engine::Environment::*member, double min,
                               double max, double step, const QString& tip)
{
    auto* edit = new Vec3Editor(min, max, step, this);
    edit->setToolTip(tip);
    form->addRow(label, edit);
    vectors_.emplace_back(edit, member);
    connect(edit, &Vec3Editor::valueEdited, this, [this, member](glm::vec3 v) {
        if (updating_)
            return;
        scene_->environment.*member = v;
        changed();
    });
}

void EnvironmentPanel::exportNeutralLut()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export neutral LUT"), assetPath("textures/luts/neutral_32.png"),
                                                      tr("PNG (*.png)"));
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    constexpr int n = 32;
    QImage image(n * n, n, QImage::Format_RGB888);
    for (int g = 0; g < n; ++g)
        for (int b = 0; b < n; ++b)
            for (int r = 0; r < n; ++r)
                image.setPixel(b * n + r, g, qRgb(r * 255 / (n - 1), g * 255 / (n - 1), b * 255 / (n - 1)));
    if (!image.save(path))
        QMessageBox::warning(this, tr("Export neutral LUT"), tr("Cannot write %1").arg(path));
}

void EnvironmentPanel::refresh()
{
    updating_ = true;
    for (auto& [edit, member] : vectors_)
        edit->setValue(scene_->environment.*member);
    lutEdit_->setText(QString::fromStdU16String(scene_->environment.lut.filename().u16string()));
    lutEdit_->setToolTip(QString::fromStdU16String(scene_->environment.lut.u16string()));
    for (const FloatField& f : floats_)
        f.spin->setValue(scene_->environment.*(f.member));
    for (const ColorField& c : colors_)
        paintButton(c.button, scene_->environment.*(c.member));
    sun_->setValue(scene_->sunDirection);
    rainSound_->setPath(QString::fromStdU16String(scene_->environment.rainSound.u16string()));
    windSound_->setPath(QString::fromStdU16String(scene_->environment.windSound.u16string()));
    thunderSound_->setPath(QString::fromStdU16String(scene_->environment.thunderSound.u16string()));
    reverb_->setCurrentText(QString::fromStdString(scene_->environment.reverb));
    ambience_->setPath(QString::fromStdU16String(scene_->environment.ambience.u16string()));
    refreshTimeOfDay();
    refreshWeatherCycle();
    refreshAnomalyStorm();
    updating_ = false;
}

void EnvironmentPanel::applyPreset(int index)
{
    const std::vector<Preset> list = presets();
    if (index < 0 || index >= static_cast<int>(list.size()))
        return;
    engine::Environment env = list[static_cast<std::size_t>(index)].env;
    env.rainSound = scene_->environment.rainSound; // presets change the look, not the level's sounds
    env.windSound = scene_->environment.windSound;
    env.thunderSound = scene_->environment.thunderSound;
    env.lut = scene_->environment.lut;
    env.reverb = scene_->environment.reverb;
    env.ambience = scene_->environment.ambience;
    env.reverbAmount = scene_->environment.reverbAmount;
    env.time = scene_->environment.time; // and not the day/night cycle
    scene_->environment = env;
    scene_->sunDirection = list[static_cast<std::size_t>(index)].sun;
    refresh();
    changed();
}

void EnvironmentPanel::changed()
{
    emit edited();
}

void EnvironmentPanel::buildTimeOfDay(QFormLayout* form)
{
    using T = engine::TimeOfDay;
    timeEnabled_ = new QCheckBox(tr("Day/night cycle"), this);
    timeEnabled_->setToolTip(tr("The sun and moon follow their real path for the latitude and date; sky, light, fog and "
                                "exposure follow the time. The colors above are the look of noon on a clear day. "
                                "Off: the sun stays at Direction."));
    form->addRow(QString(), timeEnabled_);
    connect(timeEnabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        scene_->environment.time.enabled = on;
        changed();
    });

    // Time: slider (start time of the level) and a play button that runs the cycle in the viewport.
    auto* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    hourSlider_ = new QSlider(Qt::Horizontal, row);
    hourSlider_->setRange(0, 24 * 60 - 1);
    hourSlider_->setPageStep(60);
    hourSlider_->setToolTip(tr("Time of day when the level starts"));
    hourLabel_ = new QLabel(QStringLiteral("12:00"), row);
    hourLabel_->setMinimumWidth(40);
    play_ = new QToolButton(row);
    play_->setText(QStringLiteral("▶"));
    play_->setCheckable(true);
    play_->setToolTip(tr("Preview: runs a whole day in 24 seconds in the viewport (the start time is kept)"));
    rowLayout->addWidget(hourSlider_, 1);
    rowLayout->addWidget(hourLabel_);
    rowLayout->addWidget(play_);
    form->addRow(tr("Time"), row);
    connect(hourSlider_, &QSlider::valueChanged, this, [this](int minutes) {
        if (updating_)
            return;
        setHourPreview(static_cast<float>(minutes) / 60.0f);
        if (!hourSlider_->isSliderDown() && !play_->isChecked())
            changed(); // keyboard / click: one undo step
    });
    connect(hourSlider_, &QSlider::sliderReleased, this, [this] {
        if (!play_->isChecked())
            changed();
    });
    playTimer_ = new QTimer(this);
    playTimer_->setInterval(33);
    auto* elapsed = new QElapsedTimer();
    connect(this, &QObject::destroyed, this, [elapsed] { delete elapsed; });
    connect(playTimer_, &QTimer::timeout, this, [this, elapsed] {
        const float seconds = static_cast<float>(elapsed->restart()) / 1000.0f;
        float hour = std::fmod(scene_->environment.time.hour + seconds, 24.0f); // 1 game hour per second
        setHourPreview(hour);
        updating_ = true;
        hourSlider_->setValue(static_cast<int>(hour * 60.0f));
        updating_ = false;
    });
    connect(play_, &QToolButton::toggled, this, [this, elapsed](bool on) {
        play_->setText(on ? QStringLiteral("■") : QStringLiteral("▶"));
        if (on) {
            playStartHour_ = scene_->environment.time.hour;
            if (!scene_->environment.time.enabled) {
                scene_->environment.time.enabled = true; // preview only; restored on stop
                timeEnabled_->setProperty("previewEnabled", true);
            }
            elapsed->start();
            playTimer_->start();
        } else {
            playTimer_->stop();
            if (timeEnabled_->property("previewEnabled").toBool()) {
                scene_->environment.time.enabled = false;
                timeEnabled_->setProperty("previewEnabled", false);
            }
            setHourPreview(playStartHour_);
            updating_ = true;
            hourSlider_->setValue(static_cast<int>(playStartHour_ * 60.0f));
            updating_ = false;
        }
    });

    auto addTime = [&](const QString& label, float T::*member, double min, double max, double step, int decimals, const QString& tip) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setToolTip(tip);
        spin->setKeyboardTracking(false);
        form->addRow(label, spin);
        timeFloats_.emplace_back(spin, member);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double v) {
            if (updating_)
                return;
            scene_->environment.time.*member = static_cast<float>(v);
            changed();
        });
    };
    addTime(tr("Day length"), &T::dayMinutes, 0, 1440, 1, 0, tr("Real minutes per game day in the game; 0 = the time stands still"));
    addTime(tr("Latitude"), &T::latitude, -89, 89, 1, 1, tr("Degrees north: how high the sun climbs and how long days are"));
    dayOfYear_ = new QSpinBox(this);
    dayOfYear_->setRange(1, 365);
    dayOfYear_->setToolTip(tr("Day of the year (172 = summer solstice, 355 = winter): season and day length"));
    dayOfYear_->setKeyboardTracking(false);
    form->addRow(tr("Day of year"), dayOfYear_);
    connect(dayOfYear_, &QSpinBox::valueChanged, this, [this](int v) {
        if (updating_)
            return;
        scene_->environment.time.dayOfYear = v;
        changed();
    });
    addTime(tr("North"), &T::north, -180, 180, 5, 0, tr("Turns the compass: where the sun rises in the level (0 = north is -Z)"));
    addTime(tr("Moon phase"), &T::moonPhase, 0, 1, 0.05, 2, tr("0 new moon, 0.5 full moon: the moon's shape, position and light"));
    addTime(tr("Night brightness"), &T::nightBrightness, 0, 3, 0.05, 2, tr("Moonlight and night sky; 0 = pitch black nights"));
    addTime(tr("Cloud shadows"), &T::cloudShadows, 0, 1, 0.05, 2, tr("How dark the shadows of drifting clouds are on the ground"));

    // Weather by hour: interpolated keys (wrap around midnight).
    weather_ = new QTableWidget(0, 7, this);
    weather_->setHorizontalHeaderLabels({tr("Hour"), tr("Clouds"), tr("Fog"), tr("Rain"), tr("Wind"), tr("Exposure"), tr("Storm")});
    weather_->verticalHeader()->setVisible(false);
    weather_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    weather_->setMinimumHeight(110);
    weather_->setToolTip(tr("Weather keys by hour (with the day/night cycle): clouds, fog density, rain, wind, exposure and storm "
                            "blend from key to key. No keys = the values below all day."));
    form->addRow(tr("Weather"), weather_);
    connect(weather_, &QTableWidget::cellChanged, this, [this] {
        if (!updating_)
            readWeatherTable();
    });
    auto* buttons = new QWidget(this);
    auto* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    auto* addKey = new QPushButton(tr("Add key at this time"), buttons);
    auto* removeKey = new QPushButton(tr("Remove"), buttons);
    buttonsLayout->addWidget(addKey);
    buttonsLayout->addWidget(removeKey);
    form->addRow(QString(), buttons);
    connect(addKey, &QPushButton::clicked, this, [this] {
        const engine::Environment current = engine::environmentAt(scene_->environment, scene_->sunDirection);
        engine::WeatherKey k;
        k.hour = std::round(scene_->environment.time.hour * 4.0f) / 4.0f;
        k.cloudCover = current.cloudCover;
        k.fogDensity = current.fogDensity;
        k.rain = current.rain;
        k.wind = current.wind;
        k.storm = current.storm;
        k.exposure = scene_->environment.time.keys.empty() ? scene_->environment.exposure
                                                           : engine::weatherAt(scene_->environment.time.keys, k.hour).exposure;
        scene_->environment.time.keys.push_back(k);
        std::sort(scene_->environment.time.keys.begin(), scene_->environment.time.keys.end(),
                  [](const engine::WeatherKey& a, const engine::WeatherKey& b) { return a.hour < b.hour; });
        refresh();
        changed();
    });
    connect(removeKey, &QPushButton::clicked, this, [this] {
        const int row = weather_->currentRow();
        auto& keys = scene_->environment.time.keys;
        if (row < 0 || row >= static_cast<int>(keys.size()))
            return;
        keys.erase(keys.begin() + row);
        refresh();
        changed();
    });
}

void EnvironmentPanel::setHourPreview(float hour)
{
    scene_->environment.time.hour = hour;
    char text[8];
    hourLabel_->setText(QString::fromLatin1(engine::formatHour(hour, text)));
    emit previewed();
}

void EnvironmentPanel::readWeatherTable()
{
    std::vector<engine::WeatherKey> keys;
    for (int r = 0; r < weather_->rowCount(); ++r) {
        auto value = [&](int c, float fallback) {
            const QTableWidgetItem* item = weather_->item(r, c);
            bool ok = false;
            const float v = item ? item->text().toFloat(&ok) : 0.0f;
            return ok ? v : fallback;
        };
        engine::WeatherKey k;
        k.hour = std::clamp(value(0, k.hour), 0.0f, 24.0f);
        k.cloudCover = std::clamp(value(1, k.cloudCover), 0.0f, 1.0f);
        k.fogDensity = std::clamp(value(2, k.fogDensity), 0.0f, 0.5f);
        k.rain = std::clamp(value(3, k.rain), 0.0f, 1.0f);
        k.wind = std::clamp(value(4, k.wind), 0.0f, 1.0f);
        k.exposure = std::clamp(value(5, k.exposure), 0.05f, 10.0f);
        k.storm = std::clamp(value(6, k.storm), 0.0f, 1.0f);
        keys.push_back(k);
    }
    scene_->environment.time.keys = keys;
    changed();
}

void EnvironmentPanel::refreshTimeOfDay()
{
    const engine::TimeOfDay& t = scene_->environment.time;
    timeEnabled_->setChecked(t.enabled);
    hourSlider_->setValue(static_cast<int>(t.hour * 60.0f));
    char text[8];
    hourLabel_->setText(QString::fromLatin1(engine::formatHour(t.hour, text)));
    for (auto& [spin, member] : timeFloats_)
        spin->setValue(t.*member);
    dayOfYear_->setValue(t.dayOfYear);
    weather_->setRowCount(static_cast<int>(t.keys.size()));
    for (int r = 0; r < static_cast<int>(t.keys.size()); ++r) {
        const engine::WeatherKey& k = t.keys[static_cast<std::size_t>(r)];
        const float values[7] = {k.hour, k.cloudCover, k.fogDensity, k.rain, k.wind, k.exposure, k.storm};
        for (int c = 0; c < 7; ++c)
            weather_->setItem(r, c, new QTableWidgetItem(QString::number(static_cast<double>(values[c]), 'g', 4)));
    }
}

// ---------------------------------------------------------------------------
// Weather cycle: kinds of weather (clear, fog, rain, storm...) that follow each other.

namespace {
constexpr int kKindColumns = 8;
}

void EnvironmentPanel::buildWeatherCycle(QFormLayout* form)
{
    cycleEnabled_ = new QCheckBox(tr("Change the weather over time"), this);
    cycleEnabled_->setToolTip(tr("In the game each kind of weather lasts a while, then blends into another one picked by chance. "
                                 "Scripts change it with game.set_weather(\"Storm\", seconds); console: weather Fog 30"));
    form->addRow(QString(), cycleEnabled_);
    connect(cycleEnabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        scene_->environment.weather.enabled = on;
        changed();
    });
    using C = engine::WeatherCycle;
    auto addCycle = [&](const QString& label, float C::*member, double min, double max, double step, const QString& tip) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(1);
        spin->setSuffix(tr(" min"));
        spin->setToolTip(tip);
        spin->setKeyboardTracking(false);
        form->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double v) {
            if (updating_)
                return;
            scene_->environment.weather.*member = static_cast<float>(v);
            changed();
        });
        cycleFloats_.emplace_back(spin, member);
    };
    addCycle(tr("Shortest"), &C::minMinutes, 0.1, 600, 0.5, tr("Real minutes a kind of weather lasts at least"));
    addCycle(tr("Longest"), &C::maxMinutes, 0.1, 600, 0.5, tr("Real minutes a kind of weather lasts at most"));
    addCycle(tr("Transition"), &C::transitionMinutes, 0, 60, 0.5, tr("Real minutes one kind takes to blend into the next"));
    cycleStart_ = new QComboBox(this);
    cycleStart_->setToolTip(tr("Weather when the level starts"));
    form->addRow(tr("Start with"), cycleStart_);
    connect(cycleStart_, &QComboBox::activated, this, [this](int index) {
        if (updating_)
            return;
        scene_->environment.weather.start = index <= 0 ? std::string() : cycleStart_->itemText(index).toStdString();
        changed();
    });

    kinds_ = new QTableWidget(0, kKindColumns, this);
    kinds_->setHorizontalHeaderLabels(
        {tr("Kind"), tr("Cloud"), tr("Fog"), tr("Rain"), tr("Wind"), tr("Storm"), tr("Exp."), tr("Chance")});
    kinds_->verticalHeader()->setVisible(false);
    kinds_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    kinds_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    kinds_->setMinimumHeight(170);
    kinds_->setToolTip(tr("Kinds of weather: clouds 0..1, fog density, rain 0..1, wind 0..1, storm (lightning) 0..1, exposure "
                          "multiplier and the relative chance of being picked next. They replace the values above (and the "
                          "hourly keys) while the cycle runs."));
    form->addRow(tr("Kinds"), kinds_);
    connect(kinds_, &QTableWidget::cellChanged, this, [this] {
        if (!updating_)
            readCycleTable();
    });
    auto* buttons = new QWidget(this);
    auto* row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    auto* add = new QPushButton(tr("Add"), buttons);
    auto* remove = new QPushButton(tr("Remove"), buttons);
    auto* reset = new QPushButton(tr("Built-in kinds"), buttons);
    reset->setToolTip(tr("Clear, Cloudy, Overcast, Fog, Rain and Storm with the engine's values"));
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(reset);
    form->addRow(QString(), buttons);
    connect(add, &QPushButton::clicked, this, [this] {
        engine::WeatherCycle& c = scene_->environment.weather;
        if (c.presets.empty())
            c.presets = engine::defaultWeatherPresets();
        engine::WeatherPreset p;
        p.name = tr("Kind %1").arg(c.presets.size() + 1).toStdString();
        c.presets.push_back(p);
        refresh();
        changed();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        engine::WeatherCycle& c = scene_->environment.weather;
        if (c.presets.empty())
            c.presets = engine::defaultWeatherPresets();
        const int r = kinds_->currentRow();
        if (r < 0 || r >= static_cast<int>(c.presets.size()) || c.presets.size() <= 1)
            return;
        c.presets.erase(c.presets.begin() + r);
        refresh();
        changed();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        scene_->environment.weather.presets.clear();
        refresh();
        changed();
    });
    cyclePreview_ = new QComboBox(this);
    cyclePreview_->setToolTip(tr("Shows a kind in the viewport (not saved)"));
    form->addRow(tr("Preview"), cyclePreview_);
    connect(cyclePreview_, &QComboBox::activated, this, [this](int index) { previewKind(index - 1); });
}

void EnvironmentPanel::refreshWeatherCycle()
{
    const engine::WeatherCycle& c = scene_->environment.weather;
    cycleEnabled_->setChecked(c.enabled);
    for (auto& [spin, member] : cycleFloats_)
        spin->setValue(c.*member);
    const std::vector<engine::WeatherPreset> list = c.presets.empty() ? engine::defaultWeatherPresets() : c.presets;
    cycleStart_->clear();
    cycleStart_->addItem(tr("Random"));
    const QString previewed = QString::fromStdString(scene_->environment.weatherNow.amount > 0.0f ? scene_->environment.weatherNow.current
                                                                                                    : std::string());
    cyclePreview_->clear();
    cyclePreview_->addItem(tr("Off (the values above)"));
    for (const engine::WeatherPreset& p : list) {
        cycleStart_->addItem(QString::fromStdString(p.name));
        cyclePreview_->addItem(QString::fromStdString(p.name));
    }
    cycleStart_->setCurrentIndex(std::max(0, cycleStart_->findText(QString::fromStdString(c.start))));
    if (c.start.empty())
        cycleStart_->setCurrentIndex(0);
    cyclePreview_->setCurrentIndex(previewed.isEmpty() ? 0 : std::max(0, cyclePreview_->findText(previewed)));
    kinds_->setRowCount(static_cast<int>(list.size()));
    for (int r = 0; r < static_cast<int>(list.size()); ++r) {
        const engine::WeatherPreset& p = list[static_cast<std::size_t>(r)];
        kinds_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(p.name)));
        const float values[kKindColumns - 1] = {p.cloudCover, p.fogDensity, p.rain, p.wind, p.storm, p.exposure, p.chance};
        for (int col = 1; col < kKindColumns; ++col)
            kinds_->setItem(r, col, new QTableWidgetItem(QString::number(static_cast<double>(values[col - 1]), 'g', 4)));
    }
}

void EnvironmentPanel::readCycleTable()
{
    std::vector<engine::WeatherPreset> list;
    for (int r = 0; r < kinds_->rowCount(); ++r) {
        auto value = [&](int col, float fallback) {
            const QTableWidgetItem* item = kinds_->item(r, col);
            bool ok = false;
            const float v = item ? item->text().toFloat(&ok) : 0.0f;
            return ok ? v : fallback;
        };
        engine::WeatherPreset p;
        if (const QTableWidgetItem* item = kinds_->item(r, 0); item && !item->text().trimmed().isEmpty())
            p.name = item->text().trimmed().toStdString();
        p.cloudCover = std::clamp(value(1, p.cloudCover), 0.0f, 1.0f);
        p.fogDensity = std::clamp(value(2, p.fogDensity), 0.0f, 0.5f);
        p.rain = std::clamp(value(3, p.rain), 0.0f, 1.0f);
        p.wind = std::clamp(value(4, p.wind), 0.0f, 1.0f);
        p.storm = std::clamp(value(5, p.storm), 0.0f, 1.0f);
        p.exposure = std::clamp(value(6, p.exposure), 0.05f, 10.0f);
        p.chance = std::max(0.0f, value(7, p.chance));
        list.push_back(p);
    }
    // Unchanged built-in kinds stay implicit (the level file keeps no copy).
    scene_->environment.weather.presets = list == engine::defaultWeatherPresets() ? std::vector<engine::WeatherPreset>{} : list;
    changed();
    const int preview = cyclePreview_->currentIndex();
    if (preview > 0)
        previewKind(preview - 1); // edits show at once
}

void EnvironmentPanel::buildAnomalyStorm(QFormLayout* form)
{
    stormEnabled_ = new QCheckBox(tr("Anomaly storms in this level"), this);
    stormEnabled_->setToolTip(tr("Every so often a siren warns of a storm: the sky turns rust red, the ambience goes quiet and then "
                                 "everyone outdoors gets hurt until it passes. NPCs run under the nearest roof (an interior volume or "
                                 "anything solid above), dog packs lie low at their lair. Console: storm [warning seconds]; scripts: "
                                 "game.start_storm(), game.storm()"));
    form->addRow(QString(), stormEnabled_);
    connect(stormEnabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        scene_->environment.anomalyStorm.enabled = on;
        changed();
    });
    using A = engine::AnomalyStormSettings;
    auto add = [&](const QString& label, float A::*member, double min, double max, double step, const QString& suffix, const QString& tip) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(1);
        spin->setSuffix(suffix);
        spin->setToolTip(tip);
        spin->setKeyboardTracking(false);
        form->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double v) {
            if (updating_)
                return;
            scene_->environment.anomalyStorm.*member = static_cast<float>(v);
            changed();
        });
        stormFloats_.emplace_back(spin, member);
    };
    add(tr("Every (shortest)"), &A::minMinutes, 0.5, 600, 1, tr(" min"), tr("Real minutes from one storm to the next, at least"));
    add(tr("Every (longest)"), &A::maxMinutes, 0.5, 600, 1, tr(" min"), tr("Real minutes from one storm to the next, at most"));
    add(tr("Warning"), &A::warningSeconds, 0, 600, 5, tr(" s"), tr("Seconds from the siren to the storm: time to find a roof"));
    add(tr("Duration"), &A::durationSeconds, 1, 1200, 5, tr(" s"), tr("Seconds the storm rages (then it fades for 25 s)"));
    add(tr("Damage to player"), &A::damage, 0, 200, 1, tr(" hp/s"), tr("Health lost per second outdoors at the storm's peak"));
    add(tr("Damage to NPCs"), &A::npcDamage, 0, 200, 1, tr(" hp/s"),
        tr("Health NPCs lose per second outdoors (mutants belong to the Zone and are spared)"));
    stormTint_ = new QPushButton(this);
    stormTint_->setToolTip(tr("Colour of the sky, fog and picture at the storm's peak"));
    form->addRow(tr("Storm colour"), stormTint_);
    connect(stormTint_, &QPushButton::clicked, this, [this] {
        glm::vec3& c = scene_->environment.anomalyStorm.tint;
        const QColor picked = QColorDialog::getColor(QColor::fromRgbF(c.r, c.g, c.b), this, tr("Storm colour"));
        if (!picked.isValid())
            return;
        c = {static_cast<float>(picked.redF()), static_cast<float>(picked.greenF()), static_cast<float>(picked.blueF())};
        refreshAnomalyStorm();
        changed();
    });
    stormPreview_ = new QSlider(Qt::Horizontal, this);
    stormPreview_->setRange(0, 100);
    stormPreview_->setToolTip(tr("Shows the storm in the viewport (0 = off); only a preview, nothing is saved"));
    form->addRow(tr("Preview"), stormPreview_);
    connect(stormPreview_, &QSlider::valueChanged, this, [this](int v) {
        scene_->environment.anomalyStormNow.look = static_cast<float>(v) / 100.0f;
        emit previewed();
    });
}

void EnvironmentPanel::refreshAnomalyStorm()
{
    const engine::AnomalyStormSettings& a = scene_->environment.anomalyStorm;
    const bool was = updating_;
    updating_ = true;
    stormEnabled_->setChecked(a.enabled);
    for (auto& [spin, member] : stormFloats_)
        spin->setValue(a.*member);
    const QColor c = QColor::fromRgbF(std::clamp(a.tint.r, 0.0f, 1.0f), std::clamp(a.tint.g, 0.0f, 1.0f), std::clamp(a.tint.b, 0.0f, 1.0f));
    stormTint_->setText(c.name());
    stormTint_->setStyleSheet(QString("background-color: %1; color: %2").arg(c.name(), c.lightness() > 128 ? "black" : "white"));
    stormPreview_->blockSignals(true);
    stormPreview_->setValue(static_cast<int>(std::round(scene_->environment.anomalyStormNow.look * 100.0f)));
    stormPreview_->blockSignals(false);
    updating_ = was;
}

void EnvironmentPanel::previewKind(int index)
{
    engine::Environment& env = scene_->environment;
    const std::vector<engine::WeatherPreset> list = env.weather.presets.empty() ? engine::defaultWeatherPresets() : env.weather.presets;
    if (index < 0 || index >= static_cast<int>(list.size())) {
        env.weatherNow = engine::WeatherNow{};
    } else {
        env.weatherNow.state = list[static_cast<std::size_t>(index)];
        env.weatherNow.amount = 1.0f;
        env.weatherNow.current = env.weatherNow.state.name;
    }
    emit previewed();
}

