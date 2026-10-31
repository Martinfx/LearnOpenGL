#include "AnomalyPanel.h"

#include <engine/Anomaly.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

struct Override {
    const char* key;
    const char* label;
    const char* filter;
    const char* folder;
};
const Override kOverrides[4] = {
    {"effect", "Idle effect", "Particle effects (*.particle)", "particles"},
    {"burst_effect", "Burst effect", "Particle effects (*.particle)", "particles"},
    {"sound", "Loop sound", "Sounds (*.sound *.wav *.ogg)", "sounds/anomalies"},
    {"burst_sound", "Burst sound", "Sounds (*.sound *.wav *.ogg)", "sounds/anomalies"},
};

} // namespace

AnomalyPanel::AnomalyPanel(std::function<QString()> assetRoot, QWidget* parent)
    : QWidget(parent), assetRoot_(std::move(assetRoot))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);

    kind_ = new QComboBox(this);
    for (int k = 0; k < static_cast<int>(engine::AnomalyKind::Count); ++k) {
        const engine::AnomalyKindInfo& info = engine::anomalyKindInfo(static_cast<engine::AnomalyKind>(k));
        kind_->addItem(QString::fromUtf8(info.name), QString::fromLatin1(info.id));
    }
    kind_->setToolTip(tr("What the anomaly does. Changing it resets the size and the parameters to the kind's defaults."));
    form->addRow(tr("Kind"), kind_);
    description_ = new QLabel(this);
    description_->setWordWrap(true);
    description_->setObjectName("dim");
    form->addRow(description_);
    auto* sizeHint = new QLabel(tr("Field: radius = Size X / 2, height = Size Y (Transform)."), this);
    sizeHint->setWordWrap(true);
    sizeHint->setObjectName("dim");
    form->addRow(sizeHint);

    params_ = new QWidget(this);
    paramsForm_ = new QFormLayout(params_);
    paramsForm_->setContentsMargins(0, 0, 0, 0);
    paramsForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(params_);

    auto* extra = new QFormLayout();
    extra->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(extra);
    link_ = new QComboBox(this);
    link_->setToolTip(tr("Rift: entity it throws you to (e.g. another rift). Empty = a random spot at the throw distance."));
    linkRow_ = link_;
    extra->addRow(tr("Exit (link)"), link_);
    for (int i = 0; i < 4; ++i) {
        const Override& o = kOverrides[i];
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        overrides_[i] = new QLineEdit(row);
        overrides_[i]->setToolTip(tr("Relative to assets/. Empty = the kind's default (shown greyed)."));
        auto* browse = new QPushButton(QStringLiteral("..."), row);
        browse->setMaximumWidth(32);
        h->addWidget(overrides_[i], 1);
        h->addWidget(browse);
        extra->addRow(tr(o.label), row);
        connect(overrides_[i], &QLineEdit::editingFinished, this, [this, i] {
            setProp(kOverrides[i].key, overrides_[i]->text().trimmed().toStdString());
        });
        connect(browse, &QPushButton::clicked, this, [this, i] {
            const QString root = assetRoot_();
            const QString path = QFileDialog::getOpenFileName(this, tr(kOverrides[i].label), QDir(root).filePath(kOverrides[i].folder),
                                                              tr(kOverrides[i].filter));
            if (path.isEmpty())
                return;
            overrides_[i]->setText(QDir(root).relativeFilePath(path));
            setProp(kOverrides[i].key, overrides_[i]->text().toStdString());
        });
    }

    connect(kind_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (updating_ || !entity_ || index < 0)
            return;
        engine::setAnomalyKind(*entity_, static_cast<engine::AnomalyKind>(index));
        rebuild();
        emit kindChanged();
        emit edited();
    });
    connect(link_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (!updating_)
            setProp("link", name.toStdString());
    });
    setEntity(nullptr, nullptr);
}

void AnomalyPanel::setProp(const std::string& key, const std::string& value)
{
    if (updating_ || !entity_)
        return;
    const auto it = entity_->properties.find(key);
    if (value.empty() ? it == entity_->properties.end() : (it != entity_->properties.end() && it->second == value))
        return;
    if (value.empty())
        entity_->properties.erase(key);
    else
        entity_->properties[key] = value;
    // The editor refreshes the details on every edit: do not delete the field that is sending this.
    inEdit_ = true;
    emit edited();
    inEdit_ = false;
}

void AnomalyPanel::setEntity(engine::Entity* entity, const engine::Scene* scene)
{
    if (inEdit_ && entity == entity_)
        return;
    entity_ = entity;
    scene_ = scene;
    setEnabled(entity != nullptr);
    rebuild();
}

void AnomalyPanel::rebuild()
{
    updating_ = true;
    while (paramsForm_->rowCount() > 0)
        paramsForm_->removeRow(0);
    if (!entity_) {
        description_->clear();
        updating_ = false;
        return;
    }
    const engine::AnomalySettings settings = engine::anomalySettings(*entity_);
    const engine::AnomalyKindInfo& info = engine::anomalyKindInfo(settings.kind);
    kind_->setCurrentIndex(static_cast<int>(settings.kind));
    description_->setText(QString::fromUtf8(info.description));

    const auto& params = engine::anomalyParams(settings.kind);
    for (std::size_t i = 0; i < params.size(); ++i) {
        const engine::AnomalyParam& p = params[i];
        const std::string key = p.key;
        const float value = settings.values[i];
        auto store = [this, key, def = p.defaultValue](double v) {
            // Defaults are not written, so later tuning of a kind reaches every level.
            setProp(key, std::abs(v - def) < 1e-5 ? std::string() : QString::number(v, 'g', 5).toStdString());
        };
        QWidget* field = nullptr;
        if (p.toggle) {
            auto* check = new QCheckBox(params_);
            check->setChecked(value >= 0.5f);
            connect(check, &QCheckBox::toggled, this, [store](bool on) { store(on ? 1.0 : 0.0); });
            field = check;
        } else {
            auto* spin = new QDoubleSpinBox(params_);
            spin->setRange(p.min, p.max);
            spin->setSingleStep(p.step);
            spin->setDecimals(p.step < 0.1f ? 2 : p.step < 1.0f ? 1 : 0);
            if (p.step < 0.1f)
                spin->setDecimals(2);
            spin->setSuffix(QString::fromUtf8(p.unit));
            spin->setKeyboardTracking(false);
            spin->setValue(value);
            connect(spin, &QDoubleSpinBox::valueChanged, this, store);
            field = spin;
        }
        field->setToolTip(QString::fromUtf8(p.tooltip));
        paramsForm_->addRow(tr(p.label), field);
    }

    // Rift exit: any named entity of the level except this one.
    const bool rift = settings.kind == engine::AnomalyKind::Rift;
    link_->clear();
    link_->addItem(QString());
    if (scene_)
        for (const engine::Entity& e : scene_->entities)
            if (&e != entity_ && e.type != "sound" && e.type != "particle")
                link_->addItem(QString::fromStdString(e.name));
    if (!settings.link.empty() && link_->findText(QString::fromStdString(settings.link)) < 0)
        link_->addItem(QString::fromStdString(settings.link)); // missing target stays visible
    link_->setCurrentText(QString::fromStdString(settings.link));
    linkRow_->setEnabled(rift);

    const char* defaults[4] = {info.idleEffect, info.burstEffect, info.loopSound, info.burstSound};
    for (int i = 0; i < 4; ++i) {
        const auto it = entity_->properties.find(kOverrides[i].key);
        overrides_[i]->setText(it == entity_->properties.end() ? QString() : QString::fromStdString(it->second));
        overrides_[i]->setPlaceholderText(QString::fromUtf8(defaults[i]));
        overrides_[i]->parentWidget()->setEnabled(settings.kind != engine::AnomalyKind::Radiation);
    }
    updating_ = false;
}
