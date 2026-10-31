#include "ViewportPanel.h"
#include "EditorUi.h"
#include "Vec3Editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
TransformBox::TransformBox(QWidget* parent) : QFrame(parent)
{
    setObjectName("floatingPanel");
    auto* header = new QHBoxLayout();
    auto* fold = new QToolButton(this);
    fold->setArrowType(Qt::DownArrow);
    fold->setCheckable(true);
    fold->setChecked(true);
    fold->setAutoRaise(true);
    auto* title = new QLabel(tr("<b>Transform</b>"), this);
    delta_ = new QCheckBox(tr("Delta Mode"), this);
    delta_->setToolTip(tr("Typed values are added to the selection (scale: multiplied), then the fields reset"));
    header->addWidget(fold);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(delta_);

    body_ = new QWidget(this);
    auto* grid = new QGridLayout(body_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    const double steps[3] = {0.25, 15.0, 0.1};
    const double mins[3] = {-100000.0, -360.0, -1000.0};
    const double maxs[3] = {100000.0, 360.0, 1000.0};
    const char* names[3] = {"Location", "Rotation", "Scale"};
    for (int i = 0; i < 3; ++i) {
        fields_[i] = new Vec3Editor(mins[i], maxs[i], steps[i], body_);
        auto* label = new QLabel(tr(names[i]), body_);
        if (i == 2)
            scaleLabel_ = label;
        grid->addWidget(label, i, 0);
        grid->addWidget(fields_[i], i, 1);
        connect(fields_[i], &Vec3Editor::valueEdited, this, [this, i](glm::vec3 v) {
            const bool delta = delta_->isChecked();
            emit edited(i, v, delta);
            if (delta)
                resetDeltas();
        });
    }
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(4);
    layout->addLayout(header);
    layout->addWidget(body_);
    connect(fold, &QToolButton::toggled, this, [this, fold](bool open) {
        fold->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        body_->setVisible(open);
        adjustSize();
    });
    connect(delta_, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            resetDeltas();
    });
}

bool TransformBox::deltaMode() const { return delta_->isChecked(); }

void TransformBox::resetDeltas()
{
    fields_[0]->setValue(glm::vec3(0.0f));
    fields_[1]->setValue(glm::vec3(0.0f));
    fields_[2]->setValue(glm::vec3(1.0f));
}

void TransformBox::setValues(glm::vec3 location, glm::vec3 rotation, glm::vec3 scale, const QString& scaleLabel,
                             bool canRotate, bool canScale)
{
    scaleLabel_->setText(scaleLabel);
    fields_[1]->setEnabled(canRotate);
    fields_[2]->setEnabled(canScale);
    if (deltaMode())
        return;
    fields_[0]->setValue(location);
    fields_[1]->setValue(rotation);
    fields_[2]->setValue(scale);
}

// ---------------------------------------------------------------------------
ViewportPanel::ViewportPanel(engine::Scene* scene, QWidget* parent) : QWidget(parent)
{
    viewport_ = new Viewport(scene, this);
    bar_ = new QToolBar(this);
    bar_->setIconSize(QSize(16, 16));

    // Left: options, view and shading.
    auto* menuButton = new QToolButton(bar_);
    menuButton->setIcon(editorIcon("menu"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setToolTip(tr("View options"));
    options_ = new QMenu(menuButton);
    menuButton->setMenu(options_);
    bar_->addWidget(menuButton);

    viewCombo_ = new QComboBox(bar_);
    viewCombo_->addItem(editorIcon("view"), tr("Perspective"));
    viewCombo_->addItem(editorIcon("grid"), tr("Top"));
    viewCombo_->addItem(editorIcon("grid"), tr("Front"));
    viewCombo_->addItem(editorIcon("grid"), tr("Side"));
    viewCombo_->addItem(editorIcon("content"), tr("Four views"));
    viewCombo_->setToolTip(tr("Camera: perspective, orthographic top/front/side, or all four (Ctrl+1..5)"));
    bar_->addWidget(viewCombo_);
    shadingCombo_ = new QComboBox(bar_);
    shadingCombo_->addItem(editorIcon("lit"), tr("Lit"));
    shadingCombo_->addItem(editorIcon("lit", QColor(150, 150, 150)), tr("Unlit"));
    shadingCombo_->addItem(editorIcon("lit", QColor(170, 190, 220)), tr("Lighting only"));
    shadingCombo_->addItem(editorIcon("cube"), tr("Wireframe"));
    shadingCombo_->setToolTip(tr("View mode: full lighting, base colors only, lighting on grey, or wireframe"));
    bar_->addWidget(shadingCombo_);

    auto* spacer = new QWidget(bar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar_->addWidget(spacer);

    // Right: transform tools, snapping, camera speed.
    const char* toolIcons[4] = {"select", "move", "rotate", "scale"};
    const char* toolTips[4] = {"Select (Q)", "Move (W)", "Rotate (E)", "Scale (R)"};
    for (int i = 0; i < 4; ++i) {
        toolButtons_[i] = new QToolButton(bar_);
        toolButtons_[i]->setIcon(editorIcon(toolIcons[i]));
        toolButtons_[i]->setToolTip(tr(toolTips[i]));
        toolButtons_[i]->setCheckable(true);
        toolButtons_[i]->setAutoExclusive(true);
        toolButtons_[i]->setChecked(static_cast<int>(viewport_->tool()) == i);
        bar_->addWidget(toolButtons_[i]);
        connect(toolButtons_[i], &QToolButton::clicked, this, [this, i] { viewport_->setTool(static_cast<Viewport::Tool>(i)); });
    }
    connect(viewport_, &Viewport::toolChanged, this, [this](Viewport::Tool t) { toolButtons_[static_cast<int>(t)]->setChecked(true); });
    bar_->addSeparator();

    auto snapPair = [this](const char* icon, const QString& tip, QToolButton*& on, QComboBox*& combo,
                           std::initializer_list<double> values, int current, auto format) {
        on = new QToolButton(bar_);
        on->setIcon(editorIcon(icon));
        on->setCheckable(true);
        on->setChecked(true);
        on->setToolTip(tip);
        combo = new QComboBox(bar_);
        for (double v : values)
            combo->addItem(format(v), v);
        combo->setCurrentIndex(current);
        combo->setToolTip(tip);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        bar_->addWidget(on);
        bar_->addWidget(combo);
        connect(on, &QToolButton::toggled, this, &ViewportPanel::applySnap);
        connect(combo, &QComboBox::currentIndexChanged, this, &ViewportPanel::applySnap);
    };
    snapPair("grid", tr("Grid snap for moving (hold Shift while dragging to move freely)"), gridOn_, grid_,
             {0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0}, 3, [](double v) { return QString::number(v); });
    snapPair("angle", tr("Angle snap for rotating"), angleOn_, angle_, {1.0, 5.0, 10.0, 15.0, 30.0, 45.0, 90.0}, 3,
             [](double v) { return QString::number(v) + QStringLiteral("°"); });
    snapPair("scalesnap", tr("Scale snap"), scaleOn_, scale_, {0.01, 0.05, 0.1, 0.25, 0.5, 1.0}, 2,
             [](double v) { return QString::number(v); });
    bar_->addSeparator();

    speed_ = new QToolButton(bar_);
    speed_->setIcon(editorIcon("camera"));
    speed_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    speed_->setPopupMode(QToolButton::InstantPopup);
    speed_->setToolTip(tr("Camera fly speed (mouse wheel while flying changes it too)"));
    auto* speedMenu = new QMenu(speed_);
    for (float s : {1.0f, 2.5f, 5.0f, 10.0f, 20.0f, 40.0f, 80.0f})
        speedMenu->addAction(tr("%1 m/s").arg(s), this, [this, s] {
            viewport_->setCameraSpeed(s);
            speed_->setText(QString::number(s, 'g', 3));
        });
    speed_->setMenu(speedMenu);
    speed_->setText(QString::number(viewport_->cameraSpeed(), 'g', 3));
    bar_->addWidget(speed_);
    connect(viewport_, &Viewport::cameraSpeedChanged, this,
            [this](float s) { speed_->setText(QString::number(s, 'f', s < 10 ? 1 : 0)); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar_);
    layout->addWidget(viewport_, 1);

    transform_ = new TransformBox(this);
    transform_->hide();
    transform_->raise();

    connect(viewCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 4)
            viewport_->setQuadView(true);
        else
            viewport_->setViewMode(static_cast<Viewport::ViewMode>(index));
        emit viewChanged(index);
    });
    connect(shadingCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) { viewport_->setShading(static_cast<engine::Shading>(index)); });
    applySnap();
}

void ViewportPanel::setView(int index) { viewCombo_->setCurrentIndex(index); }

void ViewportPanel::setShading(int index) { shadingCombo_->setCurrentIndex(index); }

void ViewportPanel::applySnap()
{
    grid_->setEnabled(gridOn_->isChecked());
    angle_->setEnabled(angleOn_->isChecked());
    scale_->setEnabled(scaleOn_->isChecked());
    viewport_->setSnap(gridOn_->isChecked() ? static_cast<float>(grid_->currentData().toDouble()) : 0.0f,
                       angleOn_->isChecked() ? static_cast<float>(angle_->currentData().toDouble()) : 0.0f,
                       scaleOn_->isChecked() ? static_cast<float>(scale_->currentData().toDouble()) : 0.0f);
}

void ViewportPanel::placeOverlay()
{
    transform_->adjustSize();
    transform_->move(10, bar_->height() + 30);
    transform_->raise();
}

void ViewportPanel::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    placeOverlay();
}
