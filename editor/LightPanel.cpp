#include "LightPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>

LightPanel::LightPanel(QWidget* parent) : QWidget(parent)
{
    form_ = new QFormLayout(this);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    type_ = new QComboBox(this);
    type_->addItem(tr("Point (bulb, fire)"));
    type_->addItem(tr("Spot (lamp, reflector)"));
    auto spin = [this](double min, double max, double step, const QString& suffix, const QString& tip) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(2);
        s->setSuffix(suffix);
        s->setToolTip(tip);
        s->setKeyboardTracking(false);
        return s;
    };
    intensity_ = spin(0.0, 10000.0, 1.0, QString(), tr("Brightness: at 1 m the light gives half of color x intensity, falling off with distance squared"));
    range_ = spin(0.1, 200.0, 0.5, tr(" m"), tr("No light beyond this distance (it fades out smoothly before it)"));
    inner_ = spin(0.0, 89.0, 1.0, QStringLiteral("°"), tr("Spot: angle from the axis with full brightness"));
    outer_ = spin(1.0, 89.0, 1.0, QStringLiteral("°"), tr("Spot: angle from the axis where the light ends"));
    pitch_ = spin(-90.0, 90.0, 5.0, QStringLiteral("°"), tr("Spot: tilt of the axis, -90 = straight down; the heading is the entity's Yaw"));
    shadows_ = new QCheckBox(tr("Casts shadows"), this);
    shadows_->setToolTip(tr("The nearest shadow-casting lights in view get shadows (a spot light costs one shadow map, a point light six)"));
    flicker_ = new QComboBox(this);
    flicker_->addItem(tr("Steady"));
    flicker_->addItem(tr("Fire"));
    flicker_->addItem(tr("Fluorescent tube"));
    flicker_->addItem(tr("Broken"));
    flicker_->addItem(tr("Pulse"));
    flickerSpeed_ = spin(0.05, 10.0, 0.1, QStringLiteral("x"), tr("Tempo of the flicker pattern"));
    enabled_ = new QCheckBox(tr("On"), this);
    enabled_->setToolTip(tr("Unchecked: the light starts switched off (property \"enabled\" = 0)"));
    form_->addRow(tr("Light"), type_);
    form_->addRow(tr("Intensity"), intensity_);
    form_->addRow(tr("Range"), range_);
    form_->addRow(tr("Inner angle"), inner_);
    form_->addRow(tr("Outer angle"), outer_);
    form_->addRow(tr("Pitch"), pitch_);
    form_->addRow(QString(), shadows_);
    form_->addRow(tr("Flicker"), flicker_);
    form_->addRow(tr("Flicker speed"), flickerSpeed_);
    form_->addRow(QString(), enabled_);
    for (QDoubleSpinBox* s : {intensity_, range_, inner_, outer_, pitch_, flickerSpeed_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &LightPanel::apply);
    for (QComboBox* c : {type_, flicker_})
        connect(c, &QComboBox::currentIndexChanged, this, &LightPanel::apply);
    for (QCheckBox* c : {shadows_, enabled_})
        connect(c, &QCheckBox::toggled, this, &LightPanel::apply);
    setEntity(nullptr);
}

void LightPanel::setEntity(engine::Entity* entity)
{
    entity_ = entity;
    setEnabled(entity != nullptr);
    refresh();
}

void LightPanel::refresh()
{
    const engine::LightSettings s = entity_ ? engine::lightSettings(*entity_) : engine::LightSettings{};
    updating_ = true;
    type_->setCurrentIndex(s.type == engine::LightType::Spot ? 1 : 0);
    intensity_->setValue(s.intensity);
    range_->setValue(s.range);
    inner_->setValue(s.innerAngle);
    outer_->setValue(s.outerAngle);
    pitch_->setValue(s.pitch);
    shadows_->setChecked(s.castShadows);
    flicker_->setCurrentIndex(static_cast<int>(s.flicker));
    flickerSpeed_->setValue(s.flickerSpeed);
    flickerSpeed_->setEnabled(s.flicker != engine::LightFlicker::None);
    enabled_->setChecked(s.enabled);
    const bool spot = s.type == engine::LightType::Spot;
    form_->setRowVisible(inner_, spot);
    form_->setRowVisible(outer_, spot);
    form_->setRowVisible(pitch_, spot);
    updating_ = false;
}

void LightPanel::apply()
{
    if (updating_ || !entity_)
        return;
    engine::LightSettings s;
    s.type = type_->currentIndex() == 1 ? engine::LightType::Spot : engine::LightType::Point;
    s.intensity = static_cast<float>(intensity_->value());
    s.range = static_cast<float>(range_->value());
    s.outerAngle = static_cast<float>(outer_->value());
    s.innerAngle = std::min(static_cast<float>(inner_->value()), s.outerAngle);
    s.pitch = static_cast<float>(pitch_->value());
    s.castShadows = shadows_->isChecked();
    s.flicker = static_cast<engine::LightFlicker>(flicker_->currentIndex());
    s.flickerSpeed = static_cast<float>(flickerSpeed_->value());
    s.enabled = enabled_->isChecked();
    engine::setLightSettings(*entity_, s);
    refresh();
    emit edited();
}
