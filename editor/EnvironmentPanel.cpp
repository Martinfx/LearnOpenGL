#include "EnvironmentPanel.h"
#include <engine/Audio.h>
#include "SoundEditor.h"
#include "Vec3Editor.h"
#include "AssetPaths.h"

#include <QColorDialog>
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
    section(tr("Sky"));
    addColor(form, tr("Zenith"), &E::skyZenith);
    addColor(form, tr("Horizon"), &E::skyHorizon);
    addColor(form, tr("Ground"), &E::groundColor);
    addFloat(form, tr("Clouds"), &E::cloudCover, 0, 1, 0.05, 2, tr("0 clear, 1 overcast"));
    addFloat(form, tr("Sky light"), &E::ambientIntensity, 0, 5, 0.05, 2, tr("Ambient light and reflections from the sky"));
    section(tr("Fog"));
    addColor(form, tr("Color"), &E::fogColor);
    addFloat(form, tr("Density"), &E::fogDensity, 0, 0.5, 0.002, 3);
    addFloat(form, tr("Height"), &E::fogHeight, -100, 100, 0.5, 1, tr("Fog is densest below this height (m)"));
    addFloat(form, tr("Height falloff"), &E::fogHeightFalloff, 0, 2, 0.01, 3);
    addFloat(form, tr("Sun glow"), &E::fogSunScatter, 0, 5, 0.1, 2, tr("Fog brightens toward the sun"));
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
    addFloat(form, tr("Wind"), &E::wind, 0, 1, 0.05, 2, tr("0 calm, 1 storm: gusting wind sound volume"));
    rainSound_ = new SoundPicker(this);
    rainSound_->setToolTip(tr("Rain loop; empty = sounds/ambient/rain.sound"));
    form->addRow(tr("Rain sound"), rainSound_);
    windSound_ = new SoundPicker(this);
    windSound_->setToolTip(tr("Wind loop; empty = sounds/ambient/wind_storm.sound"));
    form->addRow(tr("Wind sound"), windSound_);
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
    reverb_->setCurrentText(QString::fromStdString(scene_->environment.reverb));
    ambience_->setPath(QString::fromStdU16String(scene_->environment.ambience.u16string()));
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
    env.lut = scene_->environment.lut;
    env.reverb = scene_->environment.reverb;
    env.ambience = scene_->environment.ambience;
    env.reverbAmount = scene_->environment.reverbAmount;
    scene_->environment = env;
    scene_->sunDirection = list[static_cast<std::size_t>(index)].sun;
    refresh();
    changed();
}

void EnvironmentPanel::changed()
{
    emit edited();
}
