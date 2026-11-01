#include "PaletteDialog.h"
#include "AssetPaths.h"

#include <engine/AssetPalette.h>
#include <engine/JobSystem.h>

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

PaletteDialog::PaletteDialog(std::function<void(const QString&)> open, QWidget* parent)
    : QDialog(parent), open_(std::move(open)), generation_(std::make_shared<int>(0))
{
    setWindowTitle(tr("Albedo palette"));
    resize(980, 560);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Base colors (albedo map x color) of every material and glTF model in assets/. Plausible albedo stays "
                                "between about 30 (charcoal) and 240 (fresh snow) sRGB; the zone's look is muted."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* limits = new QHBoxLayout();
    const engine::PaletteLimits d;
    darkest_ = new QSpinBox(this);
    darkest_->setRange(0, 128);
    darkest_->setValue(d.darkest);
    brightest_ = new QSpinBox(this);
    brightest_->setRange(128, 255);
    brightest_->setValue(d.brightest);
    share_ = new QDoubleSpinBox(this);
    share_->setRange(0.0, 100.0);
    share_->setSuffix(" %");
    share_->setValue(d.maxShare * 100.0);
    share_->setToolTip(tr("A texture is a problem when more of its pixels than this are out of range"));
    saturation_ = new QDoubleSpinBox(this);
    saturation_->setRange(0.0, 1.0);
    saturation_->setSingleStep(0.05);
    saturation_->setValue(d.maxSaturation);
    saturation_->setToolTip(tr("Mean chroma (strongest minus weakest channel, 0..1) above this is a problem: rust and wood stay "
                               "around 0.2..0.3, prototype orange is 1"));
    for (auto [label, w] : {std::pair<QString, QWidget*>{tr("Darkest"), darkest_}, {tr("Brightest"), brightest_},
                            {tr("Out of range"), share_}, {tr("Max saturation"), saturation_}}) {
        limits->addWidget(new QLabel(label, this));
        limits->addWidget(w);
    }
    limits->addStretch(1);
    onlyProblems_ = new QCheckBox(tr("Only problems"), this);
    onlyProblems_->setChecked(true);
    limits->addWidget(onlyProblems_);
    run_ = new QPushButton(tr("Check"), this);
    limits->addWidget(run_);
    layout->addLayout(limits);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Asset"), tr("Material"), tr("Dark %"), tr("Bright %"), tr("Luminance"), tr("Chroma"), tr("Problems")});
    tree_->setRootIsDecorated(false);
    tree_->setSortingEnabled(true);
    tree_->setUniformRowHeights(true);
    tree_->header()->setSectionResizeMode(QHeaderView::Interactive);
    tree_->setColumnWidth(0, 300);
    tree_->setColumnWidth(1, 140);
    layout->addWidget(tree_, 1);
    status_ = new QLabel(this);
    layout->addWidget(status_);
    connect(run_, &QPushButton::clicked, this, &PaletteDialog::start);
    connect(onlyProblems_, &QCheckBox::toggled, this, &PaletteDialog::filter);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (open_)
            open_(item->data(0, Qt::UserRole).toString());
    });
    start();
}

PaletteDialog::~PaletteDialog() { ++*generation_; }

void PaletteDialog::start()
{
    ++*generation_;
    tree_->clear();
    total_ = done_ = problems_ = 0;
    engine::PaletteLimits limits;
    limits.darkest = darkest_->value();
    limits.brightest = brightest_->value();
    limits.maxShare = static_cast<float>(share_->value() / 100.0);
    limits.maxSaturation = static_cast<float>(saturation_->value());
    QStringList files;
    QDirIterator it(assetRoot(), {"*.material", "*.gltf", "*.glb"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (!f.contains("/source/") && !f.contains("/fonts/"))
            files << f;
    }
    total_ = static_cast<int>(files.size());
    status_->setText(tr("Checking %1 assets...").arg(total_));
    // One job per file on the job system; each posts its rows back to the UI thread.
    QPointer<PaletteDialog> self(this);
    const std::shared_ptr<int> generation = generation_;
    const int mine = *generation_;
    const QDir root(assetRoot());
    for (const QString& f : files) {
        const QString relative = root.relativeFilePath(f);
        engine::JobSystem::global().run([self, generation, mine, f, relative, limits] {
            if (*generation != mine)
                return; // a newer scan started or the dialog closed
            const std::vector<engine::PaletteEntry> entries = engine::checkAssetPalette(std::filesystem::path(f.toStdU16String()), limits);
            QMetaObject::invokeMethod(qApp, [self, generation, mine, f, relative, entries] {
                if (!self || *generation != mine)
                    return;
                for (const engine::PaletteEntry& e : entries) {
                    auto* item = new QTreeWidgetItem();
                    item->setText(0, relative);
                    item->setData(0, Qt::UserRole, f);
                    item->setText(1, QString::fromStdString(e.part));
                    item->setData(2, Qt::DisplayRole, std::round(e.stats.tooDark * 1000.0) / 10.0);
                    item->setData(3, Qt::DisplayRole, std::round(e.stats.tooBright * 1000.0) / 10.0);
                    item->setData(4, Qt::DisplayRole, static_cast<int>(std::lround(e.stats.meanLuminance)));
                    item->setData(5, Qt::DisplayRole, std::round(e.stats.meanSaturation * 100.0) / 100.0);
                    QStringList problems;
                    for (const std::string& p : e.problems)
                        problems << QString::fromStdString(p);
                    item->setText(6, problems.join("; "));
                    item->setToolTip(6, problems.join("\n"));
                    item->setData(6, Qt::UserRole, !e.problems.empty());
                    if (!e.problems.empty()) {
                        ++self->problems_;
                        item->setForeground(6, QColor(230, 150, 60));
                    }
                    self->tree_->addTopLevelItem(item);
                    item->setHidden(self->onlyProblems_->isChecked() && e.problems.empty());
                }
                ++self->done_;
                self->status_->setText(self->done_ < self->total_
                                           ? tr("Checking... %1 / %2 assets, %3 problems").arg(self->done_).arg(self->total_).arg(self->problems_)
                                           : tr("%1 assets checked, %2 materials with problems (double-click opens the asset)")
                                                 .arg(self->total_)
                                                 .arg(self->problems_));
            });
        });
    }
}

void PaletteDialog::filter()
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree_->topLevelItem(i);
        item->setHidden(onlyProblems_->isChecked() && !item->data(6, Qt::UserRole).toBool());
    }
}
