// Mesh paint mode: brush vertex colours onto the selected model. Red and green blend in
// layer 1 and 2 of its materials (set in the material editor), blue makes it wet.
#include "MainWindow.h"
#include "AssetPaths.h"
#include "EditorUi.h"
#include "Viewport.h"
#include <engine/VertexPaint.h>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

struct MeshPaintUi {
    QLabel* info = nullptr;
    QLabel* layers = nullptr;
    QButtonGroup* channel = nullptr;
    QDoubleSpinBox* radius = nullptr;
    QDoubleSpinBox* strength = nullptr;
    QCheckBox* erase = nullptr;
    QPushButton* clear = nullptr;
    QElapsedTimer clock;
    bool stroke = false, changed = false;
};

namespace {
const glm::vec3 kChannelColors[3] = {{1.0f, 0.35f, 0.3f}, {0.4f, 1.0f, 0.4f}, {0.35f, 0.6f, 1.0f}};
}

void MainWindow::buildMeshPaintPage()
{
    meshPaintUi_ = new MeshPaintUi;
    MeshPaintUi& ui = *meshPaintUi_;
    auto* page = new QWidget(modePages_);
    auto* layout = new QVBoxLayout(page);
    auto* title = new QLabel(tr("<b>Mesh paint</b><br>Select a model and paint over it with the left mouse button. "
                                "<span style='color:#f66'>Layer 1</span> and <span style='color:#6f6'>layer 2</span> blend in the "
                                "blend layers of the model's materials (moss, rust, dirt: set them in the material editor), "
                                "<span style='color:#69f'>Wet</span> darkens it and makes it glossy. Shift erases."),
                            page);
    title->setWordWrap(true);
    layout->addWidget(title);
    ui.info = new QLabel(page);
    ui.info->setObjectName("dim");
    ui.info->setWordWrap(true);
    layout->addWidget(ui.info);
    ui.layers = new QLabel(page);
    ui.layers->setWordWrap(true);
    layout->addWidget(ui.layers);

    auto* form = new QFormLayout;
    auto* channels = new QHBoxLayout;
    ui.channel = new QButtonGroup(page);
    const QString names[] = {tr("Layer 1"), tr("Layer 2"), tr("Wet")};
    const QString tips[] = {tr("Red: blend layer 1 of the materials"), tr("Green: blend layer 2 of the materials"),
                            tr("Blue: wet surface (darker, glossy): leaks, damp corners")};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(names[i], page);
        b->setCheckable(true);
        b->setObjectName("tool");
        b->setToolTip(tips[i]);
        b->setChecked(i == 0);
        ui.channel->addButton(b, i);
        channels->addWidget(b);
    }
    form->addRow(tr("Paint"), channels);
    ui.radius = new QDoubleSpinBox(page);
    ui.radius->setRange(0.05, 20.0);
    ui.radius->setSingleStep(0.1);
    ui.radius->setValue(0.75);
    ui.radius->setSuffix(" m");
    ui.radius->setToolTip(tr("Brush radius around the cursor on the model"));
    ui.strength = new QDoubleSpinBox(page);
    ui.strength->setRange(0.01, 1.0);
    ui.strength->setSingleStep(0.05);
    ui.strength->setValue(0.5);
    ui.strength->setToolTip(tr("How fast the paint builds up while the button is held"));
    ui.erase = new QCheckBox(tr("Erase (or hold Shift)"), page);
    form->addRow(tr("Radius"), ui.radius);
    form->addRow(tr("Strength"), ui.strength);
    form->addRow(QString(), ui.erase);
    layout->addLayout(form);
    ui.clear = new QPushButton(editorIcon("delete"), tr("Clear the model's paint"), page);
    layout->addWidget(ui.clear);
    auto* hint = new QLabel(tr("Paint is stored per vertex: models with more vertices take finer strokes. "
                               "It is saved in the level, per placed copy."),
                            page);
    hint->setObjectName("dim");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(1);

    connect(ui.channel, &QButtonGroup::idClicked, this, [this] { updateTerrainBrush(); });
    connect(ui.erase, &QCheckBox::toggled, this, [this] { updateTerrainBrush(); });
    connect(ui.radius, &QDoubleSpinBox::valueChanged, this, [this] { updateTerrainBrush(); });
    connect(ui.clear, &QPushButton::clicked, this, [this] {
        engine::Model* m = selectedModel();
        if (!m || !m->paint)
            return;
        m->paint.reset(); // undo snapshots keep the old paint
        markDirty();
        refreshMeshPaintPanel();
        viewport_->update();
    });

    auto* scroll = new QScrollArea(modePages_);
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    modePages_->addWidget(scroll);
}

void MainWindow::refreshMeshPaintPanel()
{
    if (!meshPaintUi_)
        return;
    MeshPaintUi& ui = *meshPaintUi_;
    const engine::Model* m = selectedModel();
    ui.clear->setEnabled(m && m->paint);
    if (!m) {
        ui.info->setText(tr("No model selected: click one in the viewport (Select mode) or the outliner."));
        ui.layers->clear();
        return;
    }
    std::size_t vertices = 0;
    for (const engine::MeshPart& part : m->parts)
        vertices += part.positions.size();
    ui.info->setText(tr("%1: %n vertices", nullptr, static_cast<int>(vertices)).arg(QString::fromStdString(m->name)));
    // Which layers its materials have: painting without them only shows wetness.
    QStringList lines;
    for (const auto& [slot, material] : m->overrideMaterials) {
        if (!material)
            continue;
        QStringList names;
        for (int i = 0; i < engine::Material::kBlendLayers; ++i)
            names << (material->layers[static_cast<std::size_t>(i)]
                          ? QString::fromStdString(material->layers[static_cast<std::size_t>(i)]->name)
                          : tr("none"));
        lines << tr("%1: layer 1 %2, layer 2 %3").arg(QString::fromStdString(material->name), names[0], names[1]);
    }
    ui.layers->setText(lines.isEmpty() ? tr("The model has no engine materials (Model mode > material slots): only Wet shows.")
                                       : lines.join("<br>"));
}

void MainWindow::updateMeshPaintBrush()
{
    if (!meshPaintUi_)
        return;
    const MeshPaintUi& ui = *meshPaintUi_;
    const int channel = std::clamp(ui.channel->checkedId(), 0, 2);
    const glm::vec3 color = ui.erase->isChecked() ? glm::vec3(0.8f) : kChannelColors[channel];
    viewport_->setBrush(selectedModel() != nullptr, static_cast<float>(ui.radius->value()), color);
}

void MainWindow::meshPaintStroke(glm::vec3 point, Qt::KeyboardModifiers modifiers, bool first)
{
    engine::Model* m = selectedModel();
    if (!m || !meshPaintUi_)
        return;
    MeshPaintUi& ui = *meshPaintUi_;
    const float dt = first || !ui.clock.isValid() ? 1.0f / 30.0f : std::clamp(static_cast<float>(ui.clock.restart()) / 1000.0f, 0.0f, 0.25f);
    if (first) {
        ui.clock.restart();
        // Copy on write: undo snapshots keep the paint as it was before this stroke.
        m->paint = m->paint ? std::make_shared<engine::VertexPaint>(*m->paint) : std::make_shared<engine::VertexPaint>();
        ui.stroke = true;
        ui.changed = false;
    }
    const float s = static_cast<float>(ui.strength->value());
    const float amount = 1.0f - std::pow(1.0f - std::min(s, 0.99f), dt * 4.0f);
    const bool erase = ui.erase->isChecked() || (modifiers & Qt::ShiftModifier);
    if (engine::paintVertices(*m->paint, *m, point, static_cast<float>(ui.radius->value()), std::clamp(ui.channel->checkedId(), 0, 2),
                              amount, erase))
        ui.changed = true;
    viewport_->update();
}

void MainWindow::meshPaintFinished()
{
    if (!meshPaintUi_ || !meshPaintUi_->stroke)
        return;
    meshPaintUi_->stroke = false;
    if (engine::Model* m = selectedModel(); m && m->paint && m->paint->empty())
        m->paint.reset();
    if (meshPaintUi_->changed)
        markDirty();
    refreshMeshPaintPanel();
}
