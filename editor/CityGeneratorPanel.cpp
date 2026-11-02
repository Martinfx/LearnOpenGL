#include "CityGeneratorPanel.h"

#include "AssetPaths.h"
#include "Vec3Editor.h"

#include <engine/JobSystem.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <functional>
#include <cmath>
#include <sstream>

using namespace engine::city;
namespace fs = std::filesystem;

namespace {

fs::path toPath(const QString& s) { return fs::path(s.toStdU16String()); }
QString fromPath(const fs::path& p) { return QString::fromStdU16String(p.generic_u16string()); }

QString listText(const std::vector<float>& v)
{
    QStringList parts;
    for (float x : v)
        parts << QString::number(x, 'g', 6);
    return parts.join(' ');
}

std::vector<float> parseList(const QString& text)
{
    std::vector<float> out;
    for (const QString& part : text.split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts)) {
        bool ok = false;
        const float v = part.toFloat(&ok);
        if (ok && v > 0.0f)
            out.push_back(v);
    }
    return out;
}

QComboBox* streetCombo(StreetClass c)
{
    auto* box = new QComboBox;
    for (int i = 0; i < kStreetClassCount; ++i)
        box->addItem(QString::fromLatin1(streetClassName(static_cast<StreetClass>(i))));
    box->setCurrentIndex(static_cast<int>(c));
    return box;
}

const char* const kMaterialKeys[6] = {"Road", "Pavement", "Yard", "Bare lot", "Gate", "Gate pillar"};

} // namespace

CityGeneratorPanel::CityGeneratorPanel(QWidget* parent) : QWidget(parent)
{
    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(120);
    connect(previewTimer_, &QTimer::timeout, this, &CityGeneratorPanel::computePreview);
    buildUi();
    const QString last = QSettings().value("city/file").toString();
    loadCity(QFileInfo::exists(last) ? last : assetPath("city/zone_town.city"));
}

CityGeneratorPanel::~CityGeneratorPanel() { ++*previewGen_; }

bool CityGeneratorPanel::previewShown() const { return showPreview_ && showPreview_->isChecked(); }

QString CityGeneratorPanel::kitDir() const
{
    return settings_.kit.empty() ? assetPath("city") : QFileInfo(fromPath(settings_.kit)).absolutePath();
}

// ------------------------------------------------------------------ UI
void CityGeneratorPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* files = new QHBoxLayout;
    fileLabel_ = new QLabel(this);
    fileLabel_->setObjectName("dim");
    auto* open = new QPushButton(tr("Open..."), this);
    auto* save = new QPushButton(tr("Save"), this);
    auto* saveAs = new QPushButton(tr("Save as..."), this);
    open->setToolTip(tr("Load city generator settings (.city)"));
    files->addWidget(fileLabel_, 1);
    files->addWidget(open);
    files->addWidget(save);
    files->addWidget(saveAs);
    layout->addLayout(files);
    connect(open, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Open city settings"), assetPath("city"), tr("City settings (*.city)"));
        if (!f.isEmpty())
            loadCity(f);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        if (cityFile_.isEmpty())
            cityFile_ = QFileDialog::getSaveFileName(this, tr("Save city settings"), assetPath("city"), tr("City settings (*.city)"));
        if (!cityFile_.isEmpty())
            saveCity(cityFile_);
    });
    connect(saveAs, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save city settings"), assetPath("city"), tr("City settings (*.city)"));
        if (!f.isEmpty())
            saveCity(f);
    });

    auto* tabs = new QTabWidget(this);
    auto scroll = [this](QWidget* w) {
        auto* area = new QScrollArea(this);
        area->setWidget(w);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        return area;
    };
    tabs->addTab(scroll(layoutTab()), tr("Layout"));
    tabs->addTab(scroll(townTab()), tr("Town plan"));
    tabs->addTab(scroll(rulesTab()), tr("Rules"));
    tabs->addTab(scroll(kitTab()), tr("Kit"));
    layout->addWidget(tabs, 1);

    showPreview_ = new QCheckBox(tr("Show the layout in the viewport"), this);
    showPreview_->setToolTip(tr("Yellow: street axes, white: lots and yards, blue: buildings, green: gateways and trees, brown: paths kept clear"));
    showPreview_->setChecked(QSettings().value("city/preview", true).toBool());
    connect(showPreview_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("city/preview", on);
        emit previewChanged();
    });
    layout->addWidget(showPreview_);

    auto* actions = new QHBoxLayout;
    generate_ = new QPushButton(tr("Generate"), this);
    generate_->setToolTip(tr("Builds the town into the level (one undo step). The previous town (everything in the root group) is replaced."));
    auto* clear = new QPushButton(tr("Remove town"), this);
    clear->setToolTip(tr("Deletes the root group of the generated town and everything in it"));
    actions->addWidget(generate_, 1);
    actions->addWidget(clear);
    layout->addLayout(actions);
    connect(generate_, &QPushButton::clicked, this, [this] { generate(true); });
    connect(clear, &QPushButton::clicked, this, [this] {
        const int n = groupSize_ ? groupSize_(settings_.group) : 0;
        if (n <= 0) {
            emit message(tr("No town \"%1\" in the level").arg(QString::fromStdString(settings_.group)));
            return;
        }
        if (QMessageBox::question(this, tr("Remove town"), tr("Delete the group \"%1\" with its %2 objects?").arg(QString::fromStdString(settings_.group)).arg(n)) ==
            QMessageBox::Yes)
            emit clearRequested(QString::fromStdString(settings_.group));
    });
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setVisible(false);
    layout->addWidget(progress_);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setObjectName("dim");
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
}

QWidget* CityGeneratorPanel::layoutTab()
{
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
    auto* mode = new QComboBox;
    mode->addItem(tr("Grid (designed blocks below)"));
    mode->addItem(tr("Organic (grown street network, see Town plan)"));
    mode->setToolTip(tr("Grid: you set the columns, rows and every block. Organic: streets grow from a tensor field (Chen et al. 2008), "
                        "blocks are cut into parcels and zoned by distance from the centre, houses are built by a facade grammar."));
    form->addRow(tr("Street layout"), mode);
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (updating_)
            return;
        settings_.layout = v == 1 ? Layout::Organic : Layout::Grid;
        for (QWidget* g : gridOnly_)
            g->setEnabled(settings_.layout == Layout::Grid);
        changed();
    });
    refreshers_.push_back([this, mode] {
        mode->setCurrentIndex(settings_.layout == Layout::Organic ? 1 : 0);
        for (QWidget* g : gridOnly_)
            g->setEnabled(settings_.layout == Layout::Grid);
    });
    auto* seedRow = new QWidget;
    auto* seedLayout = new QHBoxLayout(seedRow);
    seedLayout->setContentsMargins(0, 0, 0, 0);
    auto* seed = new QSpinBox;
    seed->setRange(0, 2147483647);
    seed->setToolTip(tr("Picks among the options the rules allow; the same seed gives the same town on every computer"));
    auto* reroll = new QPushButton(tr("New seed"));
    seedLayout->addWidget(seed, 1);
    seedLayout->addWidget(reroll);
    form->addRow(tr("Seed"), seedRow);
    connect(seed, &QSpinBox::valueChanged, this, [this](int v) {
        if (updating_)
            return;
        settings_.seed = static_cast<std::uint32_t>(v);
        changed();
    });
    connect(reroll, &QPushButton::clicked, this, [seed] { seed->setValue((seed->value() * 7919 + 104729) % 1000000); });
    refreshers_.push_back([this, seed] { seed->setValue(static_cast<int>(settings_.seed)); });

    auto* originRow = new QWidget;
    auto* originLayout = new QHBoxLayout(originRow);
    originLayout->setContentsMargins(0, 0, 0, 0);
    auto* origin = new Vec3Editor(-100000, 100000, 1, originRow);
    auto* useCam = new QPushButton(tr("Camera"));
    useCam->setToolTip(tr("Centre the town under the camera (on a 1 m grid, at road level 0)"));
    originLayout->addWidget(origin, 1);
    originLayout->addWidget(useCam);
    form->addRow(tr("Centre"), originRow);
    connect(origin, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        settings_.origin = v;
        changed();
    });
    connect(useCam, &QPushButton::clicked, this, [this, origin] {
        if (!camera_)
            return;
        const glm::vec3 c = camera_();
        settings_.origin = {std::round(c.x), settings_.origin.y, std::round(c.z)};
        origin->setValue(settings_.origin);
        changed();
    });
    refreshers_.push_back([this, origin] { origin->setValue(settings_.origin); });

    auto* rotation = new QDoubleSpinBox;
    rotation->setRange(-360, 360);
    rotation->setSuffix(QStringLiteral(" °"));
    form->addRow(tr("Rotation"), rotation);
    connect(rotation, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (updating_)
            return;
        settings_.rotation = static_cast<float>(v);
        changed();
    });
    refreshers_.push_back([this, rotation] { rotation->setValue(settings_.rotation); });

    auto* group = new QLineEdit;
    group->setToolTip(tr("Root group of the generated objects in the Outliner; Generate replaces it"));
    form->addRow(tr("Root group"), group);
    connect(group, &QLineEdit::editingFinished, this, [this, group] {
        settings_.group = group->text().trimmed().toStdString();
        changed();
    });
    refreshers_.push_back([this, group] { group->setText(QString::fromStdString(settings_.group)); });

    columns_ = new QLineEdit;
    columns_->setToolTip(tr("Widths of the block columns west to east, facade line to facade line (m), e.g. \"110 130 120\""));
    rows_ = new QLineEdit;
    rows_->setToolTip(tr("Depths of the block rows north to south (m)"));
    form->addRow(tr("Columns (m)"), columns_);
    form->addRow(tr("Rows (m)"), rows_);
    auto onGrid = [this] {
        auto c = parseList(columns_->text()), r = parseList(rows_->text());
        if (c.empty() || r.empty())
            return;
        // Keep the block kinds by position when the grid grows or shrinks.
        Settings old = settings_;
        settings_.columns = c;
        settings_.rows = r;
        settings_.blocks.assign(c.size() * r.size(), BlockSettings{});
        for (int y = 0; y < static_cast<int>(r.size()) && y < static_cast<int>(old.rows.size()); ++y)
            for (int x = 0; x < static_cast<int>(c.size()) && x < static_cast<int>(old.columns.size()); ++x)
                settings_.blocks[static_cast<std::size_t>(y) * c.size() + static_cast<std::size_t>(x)] = old.block(y, x);
        settings_.normalize();
        rebuildGrid();
        changed();
    };
    connect(columns_, &QLineEdit::editingFinished, this, onGrid);
    connect(rows_, &QLineEdit::editingFinished, this, onGrid);

    form->addRow(new QLabel(tr("Streets on the grid lines (west to east):")));
    streetsX_ = new QTableWidget(1, 1);
    streetsX_->verticalHeader()->hide();
    streetsX_->setFixedHeight(64);
    form->addRow(streetsX_);
    form->addRow(new QLabel(tr("Streets on the grid lines (north to south):")));
    streetsZ_ = new QTableWidget(1, 1);
    streetsZ_->verticalHeader()->hide();
    streetsZ_->setFixedHeight(64);
    form->addRow(streetsZ_);
    form->addRow(new QLabel(tr("Blocks (north at the top):")));
    blocks_ = new QTableWidget(1, 1);
    blocks_->setMinimumHeight(160);
    blocks_->setToolTip(tr("What each block is. Select a block and press Re-roll for another version of it only."));
    form->addRow(blocks_);
    auto* rerollBlock = new QPushButton(tr("Re-roll selected blocks"));
    rerollBlock->setToolTip(tr("Another version of the selected blocks; the rest of the town stays as it is"));
    form->addRow(rerollBlock);
    gridOnly_ = {columns_, rows_, streetsX_, streetsZ_, blocks_, rerollBlock};
    connect(rerollBlock, &QPushButton::clicked, this, [this] {
        for (const QModelIndex& i : blocks_->selectionModel()->selectedIndexes()) {
            BlockSettings b = settings_.block(i.row(), i.column());
            b.seed += 1;
            settings_.setBlock(i.row(), i.column(), b);
        }
        rebuildGrid();
        changed();
    });
    return w;
}

void CityGeneratorPanel::rebuildGrid()
{
    const bool was = updating_;
    updating_ = true;
    settings_.normalize();
    columns_->setText(listText(settings_.columns));
    rows_->setText(listText(settings_.rows));
    const int C = static_cast<int>(settings_.columns.size()), R = static_cast<int>(settings_.rows.size());
    auto fillStreets = [this](QTableWidget* table, std::vector<StreetClass>& list) {
        table->clear();
        table->setColumnCount(static_cast<int>(list.size()));
        table->setRowCount(1);
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            auto* box = streetCombo(list[static_cast<std::size_t>(i)]);
            table->setHorizontalHeaderItem(i, new QTableWidgetItem(QString::number(i)));
            table->setCellWidget(0, i, box);
            connect(box, &QComboBox::currentIndexChanged, this, [this, &list, i](int v) {
                if (updating_)
                    return;
                list[static_cast<std::size_t>(i)] = static_cast<StreetClass>(v);
                changed();
            });
        }
        table->resizeColumnsToContents();
    };
    fillStreets(streetsX_, settings_.streetsX);
    fillStreets(streetsZ_, settings_.streetsZ);
    blocks_->clear();
    blocks_->setColumnCount(C);
    blocks_->setRowCount(R);
    for (int c = 0; c < C; ++c)
        blocks_->setHorizontalHeaderItem(c, new QTableWidgetItem(QString(QChar('A' + c % 26)) + QStringLiteral(" (%1 m)").arg(settings_.columns[static_cast<std::size_t>(c)])));
    for (int r = 0; r < R; ++r)
        blocks_->setVerticalHeaderItem(r, new QTableWidgetItem(QStringLiteral("%1 (%2 m)").arg(r + 1).arg(settings_.rows[static_cast<std::size_t>(r)])));
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            auto* box = new QComboBox;
            for (int k = 0; k < kBlockKindCount; ++k)
                box->addItem(tr(blockKindLabel(static_cast<BlockKind>(k))));
            const BlockSettings b = settings_.block(r, c);
            box->setCurrentIndex(static_cast<int>(b.kind));
            if (b.seed)
                box->setToolTip(tr("Re-rolled %1 times").arg(b.seed));
            blocks_->setCellWidget(r, c, box);
            connect(box, &QComboBox::currentIndexChanged, this, [this, r, c](int v) {
                if (updating_)
                    return;
                BlockSettings b = settings_.block(r, c);
                b.kind = static_cast<BlockKind>(v);
                settings_.setBlock(r, c, b);
                changed();
            });
        }
    blocks_->resizeColumnsToContents();
    updating_ = was;
}

void CityGeneratorPanel::addNumber(QFormLayout* form, const QString& label, float* value, double min, double max, double step, const QString& tip,
                                   const QString& suffix, int decimals)
{
    auto* spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setToolTip(tip);
    form->addRow(label, spin);
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, value](double x) {
        if (updating_)
            return;
        *value = static_cast<float>(x);
        changed();
    });
    refreshers_.push_back([spin, value] { spin->setValue(*value); });
}

void CityGeneratorPanel::addInteger(QFormLayout* form, const QString& label, int* value, int min, int max, const QString& tip)
{
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setToolTip(tip);
    form->addRow(label, spin);
    connect(spin, &QSpinBox::valueChanged, this, [this, value](int x) {
        if (updating_)
            return;
        *value = x;
        changed();
    });
    refreshers_.push_back([spin, value] { spin->setValue(*value); });
}

void CityGeneratorPanel::addFlag(QFormLayout* form, const QString& label, bool* value, const QString& tip)
{
    auto* check = new QCheckBox(label);
    check->setToolTip(tip);
    form->addRow(QString(), check);
    connect(check, &QCheckBox::toggled, this, [this, value](bool on) {
        if (updating_)
            return;
        *value = on;
        changed();
    });
    refreshers_.push_back([check, value] { check->setChecked(*value); });
}

QWidget* CityGeneratorPanel::townTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto section = [v](const QString& title) {
        auto* box = new QGroupBox(title);
        auto* form = new QFormLayout(box);
        v->addWidget(box);
        return form;
    };
    const QString none;
    QFormLayout* net = section(tr("Street network (organic layout)"));
    net->addRow(new QLabel(tr("Streets follow a tensor field: a grid per district, rings around the centre, bent by noise. "
                              "Main roads are traced first, side streets fill in between.")));
    addNumber(net, tr("Town size"), &settings_.townSize, 100, 4000, 10, tr("Side of the square the streets fill"));
    addNumber(net, tr("Main direction"), &settings_.roadAngle, -180, 180, 1, tr("Heading of the main grid"), QStringLiteral(" °"), 1);
    addInteger(net, tr("Districts"), &settings_.districts, 1, 12, tr("Areas with their own grid direction"));
    addNumber(net, tr("District turn"), &settings_.districtTurn, 0, 90, 1, tr("The other districts turn up to this much"), QStringLiteral(" °"), 1);
    addNumber(net, tr("Ring roads"), &settings_.radial, 0, 1, 0.05, tr("0 = straight grids, 1 = rings and radials around the centre"), none);
    addNumber(net, tr("Curvature"), &settings_.curvature, 0, 1, 0.05, tr("How much the streets bend (noise in the field)"), none);
    addNumber(net, tr("Bend scale"), &settings_.noiseScale, 20, 2000, 10, tr("Size of the bends"));
    addNumber(net, tr("Main road spacing"), &settings_.mainSpacing, 20, 1000, 5, tr("Distance between main roads"));
    addNumber(net, tr("Side street spacing"), &settings_.sideSpacing, 10, 500, 5, tr("Distance between side streets (about the block size)"));
    auto* edge = streetCombo(settings_.edgeRoad);
    edge->setToolTip(tr("Road around the town (none = the blocks end in the outskirts)"));
    net->addRow(tr("Road round the town"), edge);
    connect(edge, &QComboBox::currentIndexChanged, this, [this](int x) {
        if (updating_)
            return;
        settings_.edgeRoad = static_cast<StreetClass>(x);
        changed();
    });
    refreshers_.push_back([this, edge] { edge->setCurrentIndex(static_cast<int>(settings_.edgeRoad)); });

    QFormLayout* zone = section(tr("Parcels and zoning"));
    zone->addRow(new QLabel(tr("Blocks are zoned by their distance from the centre (share of the half town size), then cut into parcels.")));
    addNumber(zone, tr("Centre"), &settings_.centreZone, 0, 1, 0.05, tr("Inside: tenement perimeter blocks along every street"), none);
    addNumber(zone, tr("Inner town"), &settings_.innerZone, 0, 1, 0.05, tr("To here: tenements, panel micro-districts and towers; beyond: houses, garages, waste ground"), none);
    addNumber(zone, tr("Private houses"), &settings_.houseShare, 0, 1, 0.05, tr("Outer blocks cut into house plots by recursive subdivision"), none);
    addNumber(zone, tr("Ruins"), &settings_.ruinShare, 0, 1, 0.05, tr("Blocks whose houses are ruins (storeys gone, open to the sky)"), none);
    addNumber(zone, tr("Parks"), &settings_.parkShare, 0, 1, 0.05, tr("Blocks left green"), none);

    QFormLayout* ground = section(tr("Ground"));
    addFlag(ground, tr("Terrain ground (grid layout)"), &settings_.terrainGround,
            tr("The grid town stands on a painted terrain with dense grass instead of ground boxes (the organic layout always does)"));
    addNumber(ground, tr("Terrain spacing"), &settings_.groundSpacing, 0.25, 4, 0.25, tr("Distance between height samples; smaller = sharper kerbs and paths, more memory"));
    addNumber(ground, tr("Hills"), &settings_.hills, 0, 50, 0.5, tr("Height of the rolling land outside the town"));
    addNumber(ground, tr("Grass density"), &settings_.foliageDensity, 0, 4, 0.05, tr("Multiplies the ground cover of the kit (Kit tab)"), none);
    auto* cacheRow = new QWidget;
    auto* ch = new QHBoxLayout(cacheRow);
    ch->setContentsMargins(0, 0, 0, 0);
    auto* cache = new QLineEdit;
    cache->setReadOnly(true);
    cache->setToolTip(tr("Where the generated house models are written (shared by identical houses; empty = \"generated\" next to the .city file)"));
    auto* pickCache = new QPushButton(QStringLiteral("..."));
    ch->addWidget(cache, 1);
    ch->addWidget(pickCache);
    ground->addRow(tr("House models"), cacheRow);
    connect(pickCache, &QPushButton::clicked, this, [this, cache] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder for the generated houses"), fromPath(settings_.cache));
        if (d.isEmpty())
            return;
        settings_.cache = toPath(d);
        cache->setText(d);
        changed();
    });
    refreshers_.push_back([this, cache] { cache->setText(fromPath(settings_.cache)); });
    v->addStretch(1);
    return w;
}

QWidget* CityGeneratorPanel::rulesTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto section = [v](const QString& title) {
        auto* box = new QGroupBox(title);
        auto* form = new QFormLayout(box);
        v->addWidget(box);
        return form;
    };
    auto number = [this](auto&&... a) { addNumber(std::forward<decltype(a)>(a)...); };
    auto flag = [this](auto&&... a) { addFlag(std::forward<decltype(a)>(a)...); };

    const char* const classLabels[] = {"", "Alley", "Side street", "Main street"};
    for (int c = kStreetClassCount - 1; c >= 1; --c) {
        StreetProfile& p = settings_.profiles[static_cast<std::size_t>(c)];
        QFormLayout* f = section(tr(classLabels[c]));
        number(f, tr("Carriageway"), &p.carriageway, 0, 60, 0.5, tr("Road width between the kerbs"));
        number(f, tr("Pavement"), &p.pavement, 0, 20, 0.25, tr("Each side; facade to facade = carriageway + 2 x pavement"));
        flag(f, tr("Lamp posts"), &p.lamps, tr("Lamp posts along the kerbs, staggered across the street, cables lamp to lamp"));
        flag(f, tr("Planted trees"), &p.trees, tr("A row of one species on each pavement (needs 2.5 m of pavement)"));
        flag(f, tr("Cables across"), &p.crossCables, tr("Cables between the two street walls"));
    }
    QFormLayout* streets = section(tr("Street furniture"));
    number(streets, tr("Kerb height"), &settings_.kerbHeight, 0, 1, 0.01, tr("Pavements and lots above the road"));
    number(streets, tr("Lamp spacing"), &settings_.lampSpacing, 5, 200, 1, tr("Along one side; the other side is offset by half"));
    number(streets, tr("Lamp cables"), &settings_.cableChance, 0, 1, 0.05, tr("Share of lamp-to-lamp spans with a cable"), QString());
    number(streets, tr("Cables across every"), &settings_.crossCableSpacing, 3, 200, 1, tr("About this far apart along the street"));
    number(streets, tr("Tree spacing"), &settings_.treeSpacing, 2, 50, 0.5, tr("Planted rows"));
    number(streets, tr("Trees left"), &settings_.treeSurvival, 0, 1, 0.05, tr("Share of the planted trees still standing (the rest: stumps, dead trees, seedlings)"), QString());
    number(streets, tr("Wrecks per 100 m"), &settings_.wrecksPer100m, 0, 20, 0.1, tr("Parked at the kerb; side streets 70 %, alleys 30 %"), QString());
    number(streets, tr("Rubble per 100 m"), &settings_.rubblePer100m, 0, 50, 0.5, tr("Fallen bits of cornices and balconies under the facades"), QString());

    QFormLayout* per = section(tr("Perimeter blocks (tenements)"));
    number(per, tr("Gateway min"), &settings_.gatewayMin, 1, 40, 0.5, tr("Courtyard gateway between two houses"));
    number(per, tr("Gateway max"), &settings_.gatewayMax, 1, 60, 0.5, tr("The frontage is filled exactly: gateways take what the house lengths leave"));
    number(per, tr("Gateway every"), &settings_.gatewayEvery, 10, 1000, 5, tr("About one gateway per this much frontage"));
    auto* step = new QSpinBox;
    step->setRange(0, 50);
    step->setToolTip(tr("Neighbouring houses differ by at most this many floors (a gently stepping skyline)"));
    per->addRow(tr("Max floor step"), step);
    connect(step, &QSpinBox::valueChanged, this, [this](int x) {
        if (updating_)
            return;
        settings_.maxFloorStep = x;
        changed();
    });
    refreshers_.push_back([this, step] { step->setValue(settings_.maxFloorStep); });
    flag(per, tr("Tall corners"), &settings_.tallCorners, tr("The corner house is one of the tallest that fit (a corner accent)"));
    number(per, tr("Party wall overlap"), &settings_.overlap, 0, 1, 0.05, tr("Neighbouring houses overlap: no sunlit slit between them"));
    flag(per, tr("Garages in big yards"), &settings_.courtyardGarages, tr("A row of garages along the back of yards bigger than 34 x 34 m, leaving the paths free"));

    QFormLayout* mic = section(tr("Micro-districts and towers"));
    number(mic, tr("Setback"), &settings_.setback, 0, 100, 1, tr("Green strip between the street and the slabs"));
    number(mic, tr("Row gap x height"), &settings_.rowGapFactor, 0, 5, 0.1, tr("Gap between slab rows as a multiple of the taller slab's height (sunlight)"), QString());
    number(mic, tr("Row gap min"), &settings_.rowGapMin, 0, 200, 1, tr("But at least this"));
    number(mic, tr("Slab gap"), &settings_.slabGap, 0, 200, 1, tr("Between slabs in one row"));

    QFormLayout* nat = section(tr("Nature and decay"));
    number(nat, tr("Overgrowth"), &settings_.overgrowth, 0, 1, 0.05,
           tr("0 = kept, 1 = decades of nobody: grass in the kerb joints, nettles at the walls, seedlings around trees, groves in yards"), QString());
    number(nat, tr("Outskirts"), &settings_.outskirts, 0, 2000, 10, tr("Waste ground with young wood around the town; the roads run on into it (0 = none)"));
    v->addStretch(1);
    return w;
}

QWidget* CityGeneratorPanel::kitTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* pathRow = new QHBoxLayout;
    kitPath_ = new QLineEdit;
    kitPath_->setReadOnly(true);
    auto* openKit = new QPushButton(tr("Open..."));
    auto* saveKitBtn = new QPushButton(tr("Save kit"));
    pathRow->addWidget(new QLabel(tr("Kit")));
    pathRow->addWidget(kitPath_, 1);
    pathRow->addWidget(openKit);
    pathRow->addWidget(saveKitBtn);
    v->addLayout(pathRow);
    connect(openKit, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Open city kit"), kitDir(), tr("City kit (*.citykit)"));
        if (!f.isEmpty()) {
            settings_.kit = toPath(f);
            loadKitFile(f);
        }
    });
    connect(saveKitBtn, &QPushButton::clicked, this, [this] {
        QString f = fromPath(settings_.kit);
        if (f.isEmpty())
            f = QFileDialog::getSaveFileName(this, tr("Save city kit"), assetPath("city"), tr("City kit (*.citykit)"));
        if (f.isEmpty())
            return;
        std::string err;
        if (saveKit(kit_, toPath(f), &err)) {
            settings_.kit = toPath(f);
            kitPath_->setText(f);
            emit message(tr("Saved %1").arg(QFileInfo(f).fileName()));
        } else {
            QMessageBox::warning(this, tr("Save kit"), QString::fromStdString(err));
        }
    });

    auto* mats = new QGroupBox(tr("Ground materials"));
    auto* matForm = new QFormLayout(mats);
    fs::path Kit::*const matFields[6] = {&Kit::road, &Kit::pavement, &Kit::yard, &Kit::lot, &Kit::gate, &Kit::pillar};
    for (int i = 0; i < 6; ++i) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        materials_[i] = new QLineEdit;
        materials_[i]->setReadOnly(true);
        auto* pick = new QPushButton(QStringLiteral("..."));
        h->addWidget(materials_[i], 1);
        h->addWidget(pick);
        matForm->addRow(tr(kMaterialKeys[i]), row);
        const auto field = matFields[i];
        connect(pick, &QPushButton::clicked, this, [this, field, i] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Material"), assetPath("materials"), tr("Material (*.material)"));
            if (f.isEmpty())
                return;
            kit_.*field = toPath(f);
            materials_[i]->setText(QFileInfo(f).fileName());
            changed();
        });
    }
    v->addWidget(mats);

    v->addWidget(new QLabel(tr("Buildings (length along the street, facade facing +Z at \"front\"):")));
    buildings_ = new QTableWidget(0, 9);
    buildings_->setHorizontalHeaderLabels({tr("Name"), tr("Role"), tr("Length"), tr("Depth"), tr("Front"), tr("Floors"), tr("Height"), tr("Weight"), tr("Model")});
    buildings_->setToolTip(tr("Roles: tenement (perimeter blocks), panel (micro-districts), tower, garage. Sizes in metres."));
    buildings_->setMinimumHeight(200);
    v->addWidget(buildings_, 1);
    connect(buildings_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_ || item->row() >= static_cast<int>(kit_.buildings.size()))
            return;
        KitBuilding& b = kit_.buildings[static_cast<std::size_t>(item->row())];
        const QString t = item->text().trimmed();
        switch (item->column()) {
        case 0: b.name = t.toStdString(); break;
        case 1: b.role = t.toStdString(); break;
        case 2: b.length = t.toFloat(); break;
        case 3: b.depth = t.toFloat(); break;
        case 4: b.front = t.toFloat(); break;
        case 5: b.floors = t.toInt(); break;
        case 6: b.height = t.toFloat(); break;
        case 7: b.weight = t.toFloat(); break;
        default: break;
        }
        changed();
    });
    auto* bRow = new QHBoxLayout;
    auto* addB = new QPushButton(tr("Add building..."));
    addB->setToolTip(tr("Pick a model; its size is measured from the file"));
    auto* measureB = new QPushButton(tr("Measure selected"));
    measureB->setToolTip(tr("Length, depth, front and height from the model's bounds (check the values: balconies count)"));
    auto* removeB = new QPushButton(tr("Remove"));
    bRow->addWidget(addB);
    bRow->addWidget(measureB);
    bRow->addWidget(removeB);
    bRow->addStretch(1);
    v->addLayout(bRow);
    connect(addB, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Building model"), assetPath("models"), tr("glTF (*.glb *.gltf)"));
        if (!f.isEmpty())
            measure(-1, f, true);
    });
    connect(measureB, &QPushButton::clicked, this, [this] {
        const int r = buildings_->currentRow();
        if (r >= 0 && r < static_cast<int>(kit_.buildings.size()))
            measure(r, fromPath(kit_.buildings[static_cast<std::size_t>(r)].path), false);
    });
    connect(removeB, &QPushButton::clicked, this, [this] {
        const int r = buildings_->currentRow();
        if (r < 0 || r >= static_cast<int>(kit_.buildings.size()))
            return;
        kit_.buildings.erase(kit_.buildings.begin() + r);
        refreshKitTables();
        changed();
    });

    v->addWidget(new QLabel(tr("Props and plants by role:")));
    props_ = new QTableWidget(0, 10);
    props_->setHorizontalHeaderLabels({tr("Role"), tr("Model"), tr("Size"), tr("Cull"), tr("Sway"), tr("Scale min"), tr("Scale max"), tr("Yaw"),
                                       tr("Collision"), tr("Shadows")});
    props_->setToolTip(tr("Roles: %1. Size = module length (kerbs, cable spans). Sway: 0.3 trees (trunk collides), 1 grass and bushes (walk through).")
                           .arg(QString::fromStdString([] {
                               std::string s;
                               for (const auto& r : propRoles())
                                   s += (s.empty() ? "" : ", ") + r;
                               return s;
                           }())));
    props_->setMinimumHeight(220);
    v->addWidget(props_, 1);
    connect(props_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_ || item->row() >= static_cast<int>(kit_.props.size()))
            return;
        KitProp& p = kit_.props[static_cast<std::size_t>(item->row())];
        const QString t = item->text().trimmed();
        switch (item->column()) {
        case 0: p.role = t.toStdString(); break;
        case 2: p.size = t.toFloat(); break;
        case 3: p.cull = t.toFloat(); break;
        case 4: p.sway = t.toFloat(); break;
        case 5: p.scaleMin = t.toFloat(); break;
        case 6: p.scaleMax = t.toFloat(); break;
        case 7: p.yaw = t.toFloat(); break;
        case 8: p.collision = t.toInt() != 0; break;
        case 9: p.shadows = t.toInt() != 0; break;
        default: break;
        }
        changed();
    });
    auto* pRow = new QHBoxLayout;
    auto* role = new QComboBox;
    for (const auto& r : propRoles())
        role->addItem(QString::fromStdString(r));
    auto* addP = new QPushButton(tr("Add prop..."));
    auto* removeP = new QPushButton(tr("Remove"));
    pRow->addWidget(role);
    pRow->addWidget(addP);
    pRow->addWidget(removeP);
    pRow->addStretch(1);
    v->addLayout(pRow);
    connect(addP, &QPushButton::clicked, this, [this, role] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Prop model"), assetPath("models"), tr("glTF (*.glb *.gltf)"));
        if (f.isEmpty())
            return;
        KitProp p;
        p.role = role->currentText().toStdString();
        p.path = toPath(f);
        const fs::path lod = engine::findLodFile(p.path);
        p.lodPath = lod;
        const bool plant = p.role == "grass" || p.role == "weed" || p.role == "nettle" || p.role == "bush" || p.role == "meadow" || p.role == "sapling";
        p.sway = p.role == "tree" || p.role == "snag" ? 0.3f : plant ? 1.0f : 0.0f;
        p.collision = !plant && p.role != "cable" && p.role != "kerb";
        p.shadows = p.role != "grass" && p.role != "weed" && p.role != "nettle" && p.role != "meadow";
        p.cull = plant ? 80.0f : 300.0f;
        kit_.props.push_back(p);
        refreshKitTables();
        changed();
    });
    connect(removeP, &QPushButton::clicked, this, [this] {
        const int r = props_->currentRow();
        if (r < 0 || r >= static_cast<int>(kit_.props.size()))
            return;
        kit_.props.erase(kit_.props.begin() + r);
        refreshKitTables();
        changed();
    });
    v->addWidget(grammarBox());
    return w;
}

namespace {

// Columns of the facade style table: header, tip, read and write as text.
struct StyleColumn {
    const char* header;
    const char* tip;
    std::function<QString(const FacadeStyle&)> get;
    std::function<void(FacadeStyle&, const QString&)> set;
    std::string FacadeStyle::*material = nullptr; // a combo of the grammar's materials
};

const std::vector<StyleColumn>& styleColumns()
{
    auto num = [](float FacadeStyle::*f) {
        return std::make_pair([f](const FacadeStyle& s) { return QString::number(s.*f); },
                              [f](FacadeStyle& s, const QString& t) { s.*f = t.toFloat(); });
    };
    auto integer = [](int FacadeStyle::*f) {
        return std::make_pair([f](const FacadeStyle& s) { return QString::number(s.*f); },
                              [f](FacadeStyle& s, const QString& t) { s.*f = t.toInt(); });
    };
    auto text = [](std::string FacadeStyle::*f) {
        return std::make_pair([f](const FacadeStyle& s) { return QString::fromStdString(s.*f); },
                              [f](FacadeStyle& s, const QString& t) { s.*f = t.toStdString(); });
    };
    static const std::vector<StyleColumn> columns = [&] {
        std::vector<StyleColumn> c;
        auto add = [&c](const char* h, const char* tip, auto p, std::string FacadeStyle::*mat = nullptr) {
            c.push_back({h, tip, p.first, p.second, mat});
        };
        add("Name", "", text(&FacadeStyle::name));
        add("Role", "tenement (perimeter blocks), panel, khrushchyovka (micro-districts), house (private plots)", text(&FacadeStyle::role));
        add("Floors min", "", integer(&FacadeStyle::floorsMin));
        add("Floors max", "", integer(&FacadeStyle::floorsMax));
        add("Ground storey", "Height of the ground floor (m)", num(&FacadeStyle::groundStorey));
        add("Storey", "Height of the upper floors (m)", num(&FacadeStyle::storey));
        add("Bay", "Window axis spacing (m), fitted to each facade", num(&FacadeStyle::bay));
        add("Window w", "", num(&FacadeStyle::windowWidth));
        add("Window h", "", num(&FacadeStyle::windowHeight));
        add("Sill", "Sill height above the floor (m)", num(&FacadeStyle::sill));
        add("Depth min", "Building depth behind the street line (m)", num(&FacadeStyle::depthMin));
        add("Depth max", "", num(&FacadeStyle::depthMax));
        add("Width min", "Parcel width along the street (m)", num(&FacadeStyle::widthMin));
        add("Width max", "", num(&FacadeStyle::widthMax));
        add("Wall", "", text(&FacadeStyle::wall), &FacadeStyle::wall);
        add("Plinth", "", text(&FacadeStyle::plinth), &FacadeStyle::plinth);
        add("Trim", "Window frames, cornice, string course", text(&FacadeStyle::trim), &FacadeStyle::trim);
        add("Roof", "", text(&FacadeStyle::roof), &FacadeStyle::roof);
        add("Firewall", "Party walls (seen where the neighbour is lower or gone)", text(&FacadeStyle::firewall), &FacadeStyle::firewall);
        add("Cornice", "Projection of the cornice (m, 0 = none)", num(&FacadeStyle::cornice));
        add("Bands", "String course over the ground floor (0/1)", num(&FacadeStyle::bands));
        add("Loggias", "Share of window columns that are loggia stacks", num(&FacadeStyle::loggias));
        add("Balconies", "Chance of a balcony column", num(&FacadeStyle::balconies));
        add("Shops", "Chance of shop windows on the street ground floor", num(&FacadeStyle::shops));
        add("Pitched", "1 = hipped sheet roof, 0 = flat with a parapet",
            std::make_pair([](const FacadeStyle& s) { return QString::number(s.pitched ? 1 : 0); },
                           [](FacadeStyle& s, const QString& t) { s.pitched = t.toInt() != 0; }));
        add("Weight", "How often the zoning picks it among the styles of its role", num(&FacadeStyle::weight));
        return c;
    }();
    return columns;
}

// Foliage table: name, model, density, share per ground kind, then the rest.
constexpr int kFoliageFixed = 3, kFoliageTail = 6;
const char* const kFoliageTailHeaders[kFoliageTail] = {"Cull", "Far density", "Sway", "Scale min", "Scale max", "Shadows"};

} // namespace

QWidget* CityGeneratorPanel::grammarBox()
{
    auto* box = new QGroupBox(tr("Generated houses and ground cover (organic layout, terrain ground)"));
    auto* v = new QVBoxLayout(box);
    auto* texRow = new QHBoxLayout;
    textures_ = new QLineEdit;
    textures_->setReadOnly(true);
    textures_->setToolTip(tr("The zone texture sets (brick_red, panel, plaster_yellow, ...) the facade grammar puts on the generated houses"));
    auto* pickTex = new QPushButton(QStringLiteral("..."));
    texRow->addWidget(new QLabel(tr("Texture sets")));
    texRow->addWidget(textures_, 1);
    texRow->addWidget(pickTex);
    v->addLayout(texRow);
    connect(pickTex, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Texture sets"), fromPath(kit_.textures));
        if (d.isEmpty())
            return;
        kit_.textures = toPath(d);
        refreshKitTables();
        changed();
    });

    v->addWidget(new QLabel(tr("Facade styles (shape grammar: facade -> floors -> bays -> window, loggia, balcony, door, shop):")));
    const auto& cols = styleColumns();
    styles_ = new QTableWidget(0, static_cast<int>(cols.size()));
    for (int c = 0; c < static_cast<int>(cols.size()); ++c) {
        auto* h = new QTableWidgetItem(tr(cols[static_cast<std::size_t>(c)].header));
        h->setToolTip(tr(cols[static_cast<std::size_t>(c)].tip));
        styles_->setHorizontalHeaderItem(c, h);
    }
    styles_->setMinimumHeight(200);
    v->addWidget(styles_, 1);
    connect(styles_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        const auto& cols = styleColumns();
        if (updating_ || item->row() >= static_cast<int>(kit_.styles.size()) || item->column() >= static_cast<int>(cols.size()))
            return;
        cols[static_cast<std::size_t>(item->column())].set(kit_.styles[static_cast<std::size_t>(item->row())], item->text().trimmed());
        changed();
    });
    auto* sRow = new QHBoxLayout;
    auto* addS = new QPushButton(tr("Add style"));
    addS->setToolTip(tr("A copy of the selected style (or a default tenement)"));
    auto* removeS = new QPushButton(tr("Remove"));
    sRow->addWidget(addS);
    sRow->addWidget(removeS);
    sRow->addStretch(1);
    v->addLayout(sRow);
    connect(addS, &QPushButton::clicked, this, [this] {
        const int r = styles_->currentRow();
        FacadeStyle st = r >= 0 && r < static_cast<int>(kit_.styles.size()) ? kit_.styles[static_cast<std::size_t>(r)] : FacadeStyle{};
        st.name += " 2";
        kit_.styles.push_back(st);
        refreshKitTables();
        changed();
    });
    connect(removeS, &QPushButton::clicked, this, [this] {
        const int r = styles_->currentRow();
        if (r < 0 || r >= static_cast<int>(kit_.styles.size()))
            return;
        kit_.styles.erase(kit_.styles.begin() + r);
        refreshKitTables();
        changed();
    });

    v->addWidget(new QLabel(tr("Ground cover grown through the terrain (instances per m2, then the share on each kind of ground):")));
    foliage_ = new QTableWidget(0, kFoliageFixed + kGroundCount + kFoliageTail);
    QStringList headers{tr("Name"), tr("Model"), tr("Density")};
    for (int g = 0; g < kGroundCount; ++g)
        headers << QString::fromLatin1(groundName(g));
    for (const char* h : kFoliageTailHeaders)
        headers << tr(h);
    foliage_->setHorizontalHeaderLabels(headers);
    foliage_->setToolTip(tr("Density: instances per m2 where it grows fully (x the Grass density of the Town plan). "
                            "yard ... built: share on that ground (0 = never, 1 = fully). Far density: share kept beyond the LOD distance."));
    foliage_->setMinimumHeight(200);
    v->addWidget(foliage_, 1);
    connect(foliage_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_ || item->row() >= static_cast<int>(kit_.foliage.size()))
            return;
        KitFoliage& f = kit_.foliage[static_cast<std::size_t>(item->row())];
        const QString t = item->text().trimmed();
        const int c = item->column();
        if (c == 0)
            f.name = t.toStdString();
        else if (c == 2)
            f.density = t.toFloat();
        else if (c >= kFoliageFixed && c < kFoliageFixed + kGroundCount)
            f.on[static_cast<std::size_t>(c - kFoliageFixed)] = t.toFloat();
        else
            switch (c - kFoliageFixed - kGroundCount) {
            case 0: f.cull = t.toFloat(); break;
            case 1: f.farDensity = t.toFloat(); break;
            case 2: f.sway = t.toFloat(); break;
            case 3: f.scaleMin = t.toFloat(); break;
            case 4: f.scaleMax = t.toFloat(); break;
            case 5: f.shadows = t.toInt() != 0; break;
            default: break;
            }
        changed();
    });
    auto* fRow = new QHBoxLayout;
    auto* addF = new QPushButton(tr("Add plant..."));
    addF->setToolTip(tr("Pick a grass or weed model (its _lod1 file is found next to it)"));
    auto* removeF = new QPushButton(tr("Remove"));
    fRow->addWidget(addF);
    fRow->addWidget(removeF);
    fRow->addStretch(1);
    v->addLayout(fRow);
    connect(addF, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Plant model"), assetPath("models"), tr("glTF (*.glb *.gltf)"));
        if (file.isEmpty())
            return;
        KitFoliage f;
        f.name = QFileInfo(file).completeBaseName().toStdString();
        f.path = toPath(file);
        f.lodPath = engine::findLodFile(f.path);
        kit_.foliage.push_back(f);
        refreshKitTables();
        changed();
    });
    connect(removeF, &QPushButton::clicked, this, [this] {
        const int r = foliage_->currentRow();
        if (r < 0 || r >= static_cast<int>(kit_.foliage.size()))
            return;
        kit_.foliage.erase(kit_.foliage.begin() + r);
        refreshKitTables();
        changed();
    });
    return box;
}

void CityGeneratorPanel::refreshKitTables()
{
    const bool was = updating_;
    updating_ = true;
    const QDir dir(kitDir());
    kitPath_->setText(fromPath(settings_.kit));
    const fs::path* mats[6] = {&kit_.road, &kit_.pavement, &kit_.yard, &kit_.lot, &kit_.gate, &kit_.pillar};
    for (int i = 0; i < 6; ++i)
        materials_[i]->setText(mats[i]->empty() ? tr("(none)") : QFileInfo(fromPath(*mats[i])).fileName());
    buildings_->setRowCount(static_cast<int>(kit_.buildings.size()));
    for (int r = 0; r < static_cast<int>(kit_.buildings.size()); ++r) {
        const KitBuilding& b = kit_.buildings[static_cast<std::size_t>(r)];
        const QString cells[9] = {QString::fromStdString(b.name), QString::fromStdString(b.role), QString::number(b.length), QString::number(b.depth),
                                  QString::number(b.front),        QString::number(b.floors),     QString::number(b.height), QString::number(b.weight),
                                  dir.relativeFilePath(fromPath(b.path))};
        for (int c = 0; c < 9; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == 8)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            buildings_->setItem(r, c, item);
        }
    }
    buildings_->resizeColumnsToContents();
    props_->setRowCount(static_cast<int>(kit_.props.size()));
    for (int r = 0; r < static_cast<int>(kit_.props.size()); ++r) {
        const KitProp& p = kit_.props[static_cast<std::size_t>(r)];
        const QString cells[10] = {QString::fromStdString(p.role), dir.relativeFilePath(fromPath(p.path)), QString::number(p.size),
                                   QString::number(p.cull),        QString::number(p.sway),               QString::number(p.scaleMin),
                                   QString::number(p.scaleMax),    QString::number(p.yaw),                QString::number(p.collision ? 1 : 0),
                                   QString::number(p.shadows ? 1 : 0)};
        for (int c = 0; c < 10; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == 1)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            props_->setItem(r, c, item);
        }
    }
    props_->resizeColumnsToContents();

    textures_->setText(kit_.textures.empty() ? tr("(none)") : dir.relativeFilePath(fromPath(kit_.textures)));
    const auto& cols = styleColumns();
    styles_->setRowCount(static_cast<int>(kit_.styles.size()));
    for (int r = 0; r < static_cast<int>(kit_.styles.size()); ++r) {
        const FacadeStyle& st = kit_.styles[static_cast<std::size_t>(r)];
        for (int c = 0; c < static_cast<int>(cols.size()); ++c) {
            const StyleColumn& col = cols[static_cast<std::size_t>(c)];
            if (col.material) {
                auto* combo = new QComboBox;
                for (const std::string& m : grammarMaterials())
                    combo->addItem(QString::fromStdString(m));
                const QString current = QString::fromStdString(st.*col.material);
                if (combo->findText(current) < 0)
                    combo->addItem(current);
                combo->setCurrentText(current);
                styles_->setItem(r, c, nullptr);
                styles_->setCellWidget(r, c, combo);
                connect(combo, &QComboBox::currentTextChanged, this, [this, r, field = col.material](const QString& t) {
                    if (updating_ || r >= static_cast<int>(kit_.styles.size()))
                        return;
                    kit_.styles[static_cast<std::size_t>(r)].*field = t.toStdString();
                    changed();
                });
            } else {
                styles_->setItem(r, c, new QTableWidgetItem(col.get(st)));
            }
        }
    }
    styles_->resizeColumnsToContents();
    foliage_->setRowCount(static_cast<int>(kit_.foliage.size()));
    for (int r = 0; r < static_cast<int>(kit_.foliage.size()); ++r) {
        const KitFoliage& f = kit_.foliage[static_cast<std::size_t>(r)];
        QStringList cells{QString::fromStdString(f.name), dir.relativeFilePath(fromPath(f.path)), QString::number(f.density)};
        for (float share : f.on)
            cells << QString::number(share);
        cells << QString::number(f.cull) << QString::number(f.farDensity) << QString::number(f.sway) << QString::number(f.scaleMin)
              << QString::number(f.scaleMax) << QString::number(f.shadows ? 1 : 0);
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == 1)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            foliage_->setItem(r, c, item);
        }
    }
    foliage_->resizeColumnsToContents();
    updating_ = was;
}

void CityGeneratorPanel::measure(int building, const QString& file, bool append)
{
    emit message(tr("Measuring %1...").arg(QFileInfo(file).fileName()));
    QPointer<CityGeneratorPanel> self(this);
    engine::JobSystem::global().run([self, building, file, append] {
        engine::Model m;
        std::string err;
        const bool ok = engine::loadModelFile(m, toPath(file), &err);
        QMetaObject::invokeMethod(qApp, [self, building, file, append, ok, err, b = m.localBounds] {
            if (!self)
                return;
            if (!ok) {
                QMessageBox::warning(self, tr("Measure"), QString::fromStdString(err));
                return;
            }
            KitBuilding kb;
            if (!append && building >= 0 && building < static_cast<int>(self->kit_.buildings.size()))
                kb = self->kit_.buildings[static_cast<std::size_t>(building)];
            else {
                kb.name = QFileInfo(file).completeBaseName().toStdString();
                kb.path = toPath(file);
                kb.lodPath = engine::findLodFile(kb.path);
            }
            auto r1 = [](float x) { return std::round(x * 10.0f) / 10.0f; };
            kb.length = r1(b.max.x - b.min.x);
            kb.depth = r1(b.max.z - b.min.z);
            kb.front = r1(b.max.z);
            kb.height = r1(b.max.y);
            if (append)
                self->kit_.buildings.push_back(kb);
            else
                self->kit_.buildings[static_cast<std::size_t>(building)] = kb;
            self->refreshKitTables();
            self->changed();
            emit self->message(tr("%1: %2 x %3 m, %4 m high").arg(QString::fromStdString(kb.name)).arg(kb.length).arg(kb.depth).arg(kb.height));
        });
    });
}

void CityGeneratorPanel::refreshWidgets()
{
    updating_ = true;
    for (const auto& f : refreshers_)
        f();
    rebuildGrid();
    refreshKitTables();
    fileLabel_->setText(cityFile_.isEmpty() ? tr("(unsaved settings)") : QFileInfo(cityFile_).fileName());
    fileLabel_->setToolTip(cityFile_);
    updating_ = false;
}

// ------------------------------------------------------------------ files
void CityGeneratorPanel::loadCity(const QString& file)
{
    std::string err;
    Settings s;
    if (!loadSettings(s, toPath(file), &err)) {
        emit message(tr("City settings: %1").arg(QString::fromStdString(err)));
        settings_.normalize();
        refreshWidgets();
        return;
    }
    settings_ = s;
    cityFile_ = file;
    QSettings().setValue("city/file", file);
    if (settings_.kit.empty())
        settings_.kit = toPath(assetPath("city/postsoviet.citykit"));
    loadKitFile(fromPath(settings_.kit));
}

void CityGeneratorPanel::loadKitFile(const QString& file)
{
    std::string err;
    if (!loadKit(kit_, toPath(file), &err))
        emit message(tr("City kit: %1").arg(QString::fromStdString(err)));
    refreshWidgets();
    changed();
}

bool CityGeneratorPanel::saveCity(const QString& file)
{
    std::string err;
    if (!saveSettings(settings_, toPath(file), &err)) {
        QMessageBox::warning(this, tr("Save city settings"), QString::fromStdString(err));
        return false;
    }
    cityFile_ = file;
    QSettings().setValue("city/file", file);
    fileLabel_->setText(QFileInfo(file).fileName());
    emit message(tr("Saved %1").arg(QFileInfo(file).fileName()));
    return true;
}

// ------------------------------------------------------------------ preview and generation
void CityGeneratorPanel::changed()
{
    if (!updating_)
        previewTimer_->start();
}

void CityGeneratorPanel::computePreview()
{
    // The layout is computed on a worker; only the newest result is shown.
    const int gen = ++*previewGen_;
    auto token = previewGen_;
    QPointer<CityGeneratorPanel> self(this);
    engine::JobSystem::global().run([self, token, gen, s = settings_, k = kit_] {
        auto plan = std::make_shared<Plan>(engine::city::generate(s, k));
        QMetaObject::invokeMethod(qApp, [self, token, gen, plan] {
            if (!self || *token != gen)
                return;
            self->preview_.lines = plan->lines;
            self->preview_.lineColors = plan->lineColors;
            self->preview_.triangles.clear();
            self->preview_.edges = glm::vec4(0.0f);
            self->preview_.xray = false;
            const auto& st = plan->stats;
            QString text = tr("%1 objects: %2 buildings, %3 gateways, %4 m of street wall (fit %5 m), %6 lamps, %7 trees, %8 wrecks, %9 plants.")
                               .arg(plan->placements.size())
                               .arg(st.buildings)
                               .arg(st.gateways)
                               .arg(static_cast<int>(st.frontage))
                               .arg(st.worstFill, 0, 'f', 2)
                               .arg(st.lamps)
                               .arg(st.trees)
                               .arg(st.wrecks)
                               .arg(st.plants);
            for (const auto& w : plan->warnings)
                text += QStringLiteral("\n") + tr("Warning: %1").arg(QString::fromStdString(w));
            if (!self->busy_)
                self->status_->setText(text);
            emit self->previewChanged();
        });
    });
}

void CityGeneratorPanel::generate(bool confirm)
{
    if (busy_)
        return;
    const int existing = groupSize_ ? groupSize_(settings_.group) : 0;
    if (confirm && existing > 0 &&
        QMessageBox::question(this, tr("Generate town"),
                              tr("Replace the town in \"%1\" (%2 objects)? Objects you added or changed inside that group are replaced too; "
                                 "Undo brings them back.")
                                  .arg(QString::fromStdString(settings_.group))
                                  .arg(existing)) != QMessageBox::Yes)
        return;
    busy_ = true;
    generate_->setEnabled(false);
    progress_->setValue(0);
    progress_->setVisible(true);
    status_->setText(tr("Generating..."));
    QPointer<CityGeneratorPanel> self(this);
    auto progress = std::make_shared<std::atomic<int>>(0);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, progress] { progress_->setValue(progress->load()); });
    timer->start(100);
    QPointer<QTimer> timerRef(timer);
    engine::JobSystem::global().run([self, progress, timer = timerRef, s = settings_, k = kit_] {
        const Plan plan = engine::city::generate(s, k);
        auto objects = std::make_shared<CityObjects>(buildObjects(plan, [progress](float f) { progress->store(static_cast<int>(f * 100.0f)); }));
        const auto& st = plan.stats;
        const QString summary = QObject::tr("Town generated: %1 buildings, %2 m of street wall, %3 lamps, %4 trees, %5 wrecks, %6 plants")
                                    .arg(st.buildings)
                                    .arg(static_cast<int>(st.frontage))
                                    .arg(st.lamps)
                                    .arg(st.trees)
                                    .arg(st.wrecks)
                                    .arg(st.plants);
        QMetaObject::invokeMethod(qApp, [self, timer, objects, summary, root = s.group] {
            if (timer)
                timer->deleteLater();
            if (!self)
                return;
            self->busy_ = false;
            self->generate_->setEnabled(true);
            self->progress_->setVisible(false);
            QString text = summary;
            for (const auto& e : objects->errors)
                text += QStringLiteral("\n") + tr("Error: %1").arg(QString::fromStdString(e));
            self->status_->setText(text);
            emit self->generated(objects, QString::fromStdString(root), summary);
        });
    });
}
