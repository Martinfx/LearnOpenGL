#include "SurfacesEditor.h"
#include "SoundEditor.h"

#include <QComboBox>
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

namespace {

enum Column { Name, Step, StepVolume, Impact, DecalSize, Columns };

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

SurfacesEditor::SurfacesEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel(tr("What each surface sounds like when walked on or shot. Materials choose their surface in the "
                               "Material editor (Auto guesses it from the name: \"rusty_metal\" is metal)."),
                            this);
    info->setWordWrap(true);
    layout->addWidget(info);

    table_ = new QTableWidget(0, Columns, this);
    table_->setHorizontalHeaderLabels({tr("Surface"), tr("Footstep"), tr("Step volume"), tr("Bullet impact"), tr("Bullet hole (m)")});
    table_->horizontalHeader()->setSectionResizeMode(Step, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(Impact, QHeaderView::Stretch);
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
    table_->setItem(r, Name, new QTableWidgetItem(QString::fromStdString(type.name)));
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
    auto* decal = spin(0, 1, 0.01, type.decalSize, table_);
    decal->setToolTip(tr("Diameter of the bullet hole, 0 = none (water, flesh)"));
    connect(decal, &QDoubleSpinBox::valueChanged, this, &SurfacesEditor::edited);
    table_->setCellWidget(r, DecalSize, decal);
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
