#include "SurfacesEditor.h"
#include "SoundEditor.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QSpinBox>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <array>

namespace {

enum Column { Name, Step, StepVolume, Impact, Effect, Casing, DecalRow, DecalSize, Ricochet, RicochetAngle, RicochetSound, Columns };
constexpr int OriginalRole = Qt::UserRole + 1; // index into SurfacesEditor::originals_

std::filesystem::path toPath(const QString& s)
{
    return s.isEmpty() ? std::filesystem::path() : std::filesystem::path(s.toStdU16String());
}

QString toQString(const std::filesystem::path& p)
{
    return p.empty() ? QString() : QString::fromStdU16String(p.u16string());
}

QDoubleSpinBox* spin(double min, double max, double step, double value, QWidget* parent)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(2);
    s->setValue(value);
    s->setKeyboardTracking(false);
    return s;
}

} // namespace

// A .particle file in a table cell: name, browse, clear and "open in the Particle editor".
class EffectPicker : public QWidget {
public:
    EffectPicker(SurfacesEditor* owner, const QString& path, QWidget* parent) : QWidget(parent), path_(path)
    {
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(2);
        edit_ = new QLineEdit(this);
        edit_->setReadOnly(true);
        edit_->setPlaceholderText(tr("none"));
        row->addWidget(edit_, 1);
        auto* browse = new QToolButton(this);
        browse->setText("...");
        browse->setToolTip(tr("Choose a particle effect"));
        auto* open = new QToolButton(this);
        open->setText(tr("Edit"));
        open->setToolTip(tr("Open in the Particle editor"));
        auto* clear = new QToolButton(this);
        clear->setText("x");
        clear->setToolTip(tr("No effect"));
        row->addWidget(browse);
        row->addWidget(open);
        row->addWidget(clear);
        update();
        connect(browse, &QToolButton::clicked, this, [this, owner] {
            QString dir = path_.isEmpty() ? QFileInfo(owner->path()).absolutePath() + "/particles" : QFileInfo(path_).absolutePath();
            const QString f = QFileDialog::getOpenFileName(this, tr("Particle effect"), dir, tr("Particle effects (*.particle)"));
            if (f.isEmpty())
                return;
            path_ = QFileInfo(f).absoluteFilePath();
            update();
            owner->markEdited();
        });
        connect(clear, &QToolButton::clicked, this, [this, owner] {
            path_.clear();
            update();
            owner->markEdited();
        });
        connect(open, &QToolButton::clicked, this, [this, owner] {
            if (!path_.isEmpty())
                emit owner->openAsset(path_);
        });
    }
    QString path() const { return path_; }

private:
    void update()
    {
        edit_->setText(path_.isEmpty() ? QString() : QFileInfo(path_).completeBaseName());
        edit_->setToolTip(path_);
    }
    QString path_;
    QLineEdit* edit_ = nullptr;
};

SurfacesEditor::SurfacesEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel(tr("What each surface sounds and looks like when walked on or shot: footsteps, bullet impact "
                               "sound and particles, the sound of spent cases landing on it and its bullet hole. Materials "
                               "choose their surface in the Material editor (Auto guesses it from the name: \"rusty_metal\" "
                               "is metal)."),
                            this);
    info->setWordWrap(true);
    layout->addWidget(info);

    table_ = new QTableWidget(0, Columns, this);
    table_->setHorizontalHeaderLabels({tr("Surface"), tr("Footstep"), tr("Step volume"), tr("Impact sound"),
                                       tr("Impact particles"), tr("Case landing"), tr("Hole style"), tr("Hole size (m)"),
                                       tr("Ricochet chance"), tr("Ricochet below °"), tr("Ricochet sound")});
    // Pickers need room for a file name; the table scrolls sideways when the tab is narrow.
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    const int widths[Columns] = {90, 240, 90, 240, 220, 240, 100, 100, 110, 110, 240};
    for (int c = 0; c < Columns; ++c)
        table_->setColumnWidth(c, widths[c]);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table_, 1);
    connect(table_, &QTableWidget::itemChanged, this, [this] {
        refreshDefault();
        edited();
    });

    auto* row = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add surface..."), this);
    auto* remove = new QPushButton(tr("Remove"), this);
    fallback_ = new QComboBox(this);
    fallback_->setToolTip(tr("Surfaces without a row play as this one"));
    auto* saveButton = new QPushButton(tr("Save"), this);
    row->addWidget(add);
    row->addWidget(remove);
    row->addSpacing(20);
    row->addWidget(new QLabel(tr("Unknown surfaces sound like"), this));
    row->addWidget(fallback_);
    row->addStretch(1);
    row->addWidget(saveButton);
    layout->addLayout(row);

    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Add surface"), tr("Name (one word, e.g. carpet):"), QLineEdit::Normal,
                                                   QString(), &ok)
                                 .trimmed()
                                 .toLower()
                                 .replace(' ', '_');
        if (!ok || name.isEmpty())
            return;
        engine::SurfaceType t;
        t.name = name.toStdString();
        addRow(t);
        refreshDefault();
        edited();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int r = table_->currentRow();
        if (r < 0)
            return;
        table_->removeRow(r);
        refreshDefault();
        edited();
    });
    connect(fallback_, &QComboBox::activated, this, &SurfacesEditor::edited);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!save(&error))
            QMessageBox::warning(this, tr("Surfaces"), error);
    });
}

QString SurfacesEditor::title() const
{
    return tr("Surfaces") + (modified_ ? "*" : "");
}

void SurfacesEditor::addRow(const engine::SurfaceType& type)
{
    const bool was = updating_;
    updating_ = true;
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto* nameItem = new QTableWidgetItem(QString::fromStdString(type.name));
    nameItem->setData(OriginalRole, static_cast<int>(originals_.size()));
    originals_.push_back(type); // keeps fields without a column (decal atlas and grid)
    table_->setItem(r, Name, nameItem);
    auto* step = new SoundPicker(table_);
    step->setPath(toQString(type.footstep));
    connect(step, &SoundPicker::pathChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, Step, step);
    auto* stepVolume = spin(0, 2, 0.05, type.stepVolume, table_);
    connect(stepVolume, &QDoubleSpinBox::valueChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, StepVolume, stepVolume);
    auto* impact = new SoundPicker(table_);
    impact->setPath(toQString(type.impact));
    connect(impact, &SoundPicker::pathChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, Impact, impact);
    auto* effect = new EffectPicker(this, toQString(type.effect), table_);
    table_->setCellWidget(r, Effect, effect);
    auto* casing = new SoundPicker(table_);
    casing->setPath(toQString(type.casing));
    connect(casing, &SoundPicker::pathChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, Casing, casing);
    auto* holeRow = new QComboBox(table_);
    holeRow->addItem(tr("Any"), -1);
    for (int i = 0; i < std::max(1, type.decalRows); ++i)
        holeRow->addItem(i < 4 ? std::array{tr("Concrete"), tr("Metal"), tr("Wood"), tr("Soft")}[i] : tr("Row %1").arg(i + 1), i);
    holeRow->setCurrentIndex(std::max(0, holeRow->findData(type.decalRow)));
    holeRow->setToolTip(tr("Which row of the bullet hole atlas this surface uses"));
    connect(holeRow, &QComboBox::activated, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, DecalRow, holeRow);
    auto* decal = spin(0, 1, 0.01, type.decalSize, table_);
    decal->setToolTip(tr("Diameter of the bullet hole, 0 = none (water, flesh)"));
    connect(decal, &QDoubleSpinBox::valueChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, DecalSize, decal);
    auto* ricochet = spin(0, 1, 0.05, type.ricochet, table_);
    ricochet->setToolTip(tr("Chance that a bullet grazing this surface glances off and flies on (0 = never)"));
    connect(ricochet, &QDoubleSpinBox::valueChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, Ricochet, ricochet);
    auto* ricochetAngle = spin(0, 90, 1, type.ricochetAngle, table_);
    ricochetAngle->setToolTip(tr("Steeper hits (angle between shot and surface) never ricochet; the chance grows toward 0°"));
    connect(ricochetAngle, &QDoubleSpinBox::valueChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, RicochetAngle, ricochetAngle);
    auto* ricochetSound = new SoundPicker(table_);
    ricochetSound->setPath(toQString(type.ricochetSound));
    connect(ricochetSound, &SoundPicker::pathChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, RicochetSound, ricochetSound);
    updating_ = was;
}

void SurfacesEditor::refreshDefault()
{
    const bool was = updating_;
    updating_ = true;
    const QString current = fallback_->currentText();
    fallback_->clear();
    for (int r = 0; r < table_->rowCount(); ++r)
        if (auto* item = table_->item(r, Name))
            fallback_->addItem(item->text());
    fallback_->setCurrentIndex(std::max(0, fallback_->findText(current)));
    updating_ = was;
}

engine::SurfaceTable SurfacesEditor::collect() const
{
    engine::SurfaceTable t;
    t.fallback = fallback_->currentText().toStdString();
    for (int r = 0; r < table_->rowCount(); ++r) {
        engine::SurfaceType s;
        if (auto* item = table_->item(r, Name)) {
            const int original = item->data(OriginalRole).toInt();
            if (item->data(OriginalRole).isValid() && original >= 0 && original < static_cast<int>(originals_.size()))
                s = originals_[original];
        }
        if (s.decal.empty() && !path_.isEmpty()) // new rows get the shared bullet hole atlas
            s.decal = toPath(QFileInfo(path_).absolutePath() + "/textures/decals/bullet_holes_albedo.png");
        if (auto* item = table_->item(r, Name))
            s.name = item->text().trimmed().replace(' ', '_').toStdString();
        if (s.name.empty())
            continue;
        if (auto* p = qobject_cast<SoundPicker*>(table_->cellWidget(r, Step)))
            s.footstep = toPath(p->path());
        if (auto* p = qobject_cast<SoundPicker*>(table_->cellWidget(r, Impact)))
            s.impact = toPath(p->path());
        if (auto* v = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(r, StepVolume)))
            s.stepVolume = static_cast<float>(v->value());
        if (auto* p = dynamic_cast<EffectPicker*>(table_->cellWidget(r, Effect)))
            s.effect = toPath(p->path());
        if (auto* p = qobject_cast<SoundPicker*>(table_->cellWidget(r, Casing)))
            s.casing = toPath(p->path());
        if (auto* c = qobject_cast<QComboBox*>(table_->cellWidget(r, DecalRow)))
            s.decalRow = c->currentData().toInt();
        if (auto* v = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(r, Ricochet)))
            s.ricochet = static_cast<float>(v->value());
        if (auto* v = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(r, RicochetAngle)))
            s.ricochetAngle = static_cast<float>(v->value());
        if (auto* p = qobject_cast<SoundPicker*>(table_->cellWidget(r, RicochetSound)))
            s.ricochetSound = toPath(p->path());
        if (auto* v = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(r, DecalSize)))
            s.decalSize = static_cast<float>(v->value());
        t.types.push_back(std::move(s));
    }
    return t;
}

bool SurfacesEditor::load(const QString& path, QString* error)
{
    path_ = QFileInfo(path).absoluteFilePath();
    engine::SurfaceTable t;
    if (QFileInfo::exists(path_)) {
        std::string e;
        if (!engine::loadSurfaces(t, toPath(path_), &e)) {
            if (error)
                *error = QString::fromStdString(e);
            return false;
        }
    } else { // a fresh project: the standard surfaces, no sounds yet
        for (const std::string& name : engine::standardSurfaceNames()) {
            engine::SurfaceType s;
            s.name = name;
            t.types.push_back(s);
        }
    }
    updating_ = true;
    table_->setRowCount(0);
    originals_.clear();
    for (const engine::SurfaceType& s : t.types)
        addRow(s);
    refreshDefault();
    fallback_->setCurrentIndex(std::max(0, fallback_->findText(QString::fromStdString(t.fallback))));
    updating_ = false;
    modified_ = false;
    emit modificationChanged();
    return true;
}

bool SurfacesEditor::save(QString* error)
{
    std::string e;
    if (!engine::saveSurfaces(collect(), toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    modified_ = false;
    emit modificationChanged();
    return true;
}

void SurfacesEditor::edited()
{
    if (updating_)
        return;
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
}
