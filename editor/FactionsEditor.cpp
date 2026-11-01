#include "FactionsEditor.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

enum Column { Id, Name, Color, Columns };

QColor toQColor(glm::vec3 c)
{
    return QColor::fromRgbF(std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f), std::clamp(c.z, 0.0f, 1.0f));
}

QColor attitudeColor(engine::Attitude a)
{
    switch (a) {
    case engine::Attitude::Hostile: return QColor(150, 45, 40);
    case engine::Attitude::Friendly: return QColor(50, 120, 55);
    default: return QColor(85, 85, 85);
    }
}

} // namespace

FactionsEditor::FactionsEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Who fights whom. Relations go from -100 (hate) to 100 (allies); below the hostile threshold "
                               "NPCs attack on sight, between the thresholds they are neutral (fight back only when attacked). "
                               "The player starts with their faction's relations; shooting members of a faction lowers the "
                               "player's standing with it. NPCs choose their faction and group in Properties > Character."),
                            this);
    help->setWordWrap(true);
    layout->addWidget(help);

    auto* form = new QFormLayout;
    player_ = new QComboBox(this);
    player_->setToolTip(tr("The player's own faction: its relations are the player's at the start of a game"));
    hostile_ = new QDoubleSpinBox(this);
    friendly_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* s : {hostile_, friendly_}) {
        s->setRange(-100.0, 100.0);
        s->setSingleStep(5.0);
        s->setDecimals(0);
        s->setKeyboardTracking(false);
    }
    hostile_->setToolTip(tr("Relations below this are hostile"));
    friendly_->setToolTip(tr("Relations above this are friendly"));
    auto* thresholds = new QHBoxLayout;
    thresholds->addWidget(new QLabel(tr("hostile below"), this));
    thresholds->addWidget(hostile_);
    thresholds->addWidget(new QLabel(tr("friendly above"), this));
    thresholds->addWidget(friendly_);
    thresholds->addStretch(1);
    form->addRow(tr("Player faction"), player_);
    form->addRow(tr("Thresholds"), thresholds);
    layout->addLayout(form);

    auto* split = new QSplitter(Qt::Vertical, this);
    auto* top = new QWidget(split);
    auto* topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    factions_ = new QTableWidget(0, Columns, top);
    factions_->setHorizontalHeaderLabels({tr("Id"), tr("Name"), tr("Colour")});
    factions_->horizontalHeader()->setSectionResizeMode(Name, QHeaderView::Stretch);
    factions_->verticalHeader()->hide();
    factions_->setSelectionBehavior(QAbstractItemView::SelectRows);
    factions_->setSelectionMode(QAbstractItemView::SingleSelection);
    factions_->setToolTip(tr("Id: what levels and scripts use (entity property faction). Name: shown to the player. "
                             "Double-click the colour to change it."));
    topLayout->addWidget(factions_);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add faction"), top);
    auto* remove = new QPushButton(tr("Remove"), top);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    topLayout->addLayout(buttons);
    matrix_ = new QTableWidget(split);
    matrix_->setToolTip(tr("Relation of the row faction to the column faction (the same both ways). "
                           "The diagonal is how members of one faction treat each other."));
    split->addWidget(top);
    split->addWidget(matrix_);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);

    connect(player_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updating_ || i < 0)
            return;
        table_.playerFaction = player_->itemData(i).toString().toStdString();
        edited();
    });
    auto thresholdsChanged = [this] {
        if (updating_)
            return;
        table_.hostileBelow = static_cast<float>(std::min(hostile_->value(), friendly_->value()));
        table_.friendlyAbove = static_cast<float>(std::max(hostile_->value(), friendly_->value()));
        refreshMatrix();
        edited();
    };
    connect(hostile_, &QDoubleSpinBox::valueChanged, this, thresholdsChanged);
    connect(friendly_, &QDoubleSpinBox::valueChanged, this, thresholdsChanged);

    connect(factions_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_)
            return;
        const int row = item->row();
        if (row < 0 || row >= static_cast<int>(table_.factions.size()))
            return;
        engine::Faction& f = table_.factions[static_cast<std::size_t>(row)];
        if (item->column() == Id) {
            const std::string id = item->text().trimmed().toStdString();
            if (id.empty() || id == "player" || (id != f.id && table_.find(id))) {
                QMessageBox::warning(this, tr("Factions"), tr("The id must be unique, not empty and not \"player\"."));
                refresh();
                return;
            }
            renameFaction(f.id, id);
        } else if (item->column() == Name) {
            f.name = item->text().toStdString();
        }
        refresh();
        edited();
    });
    connect(factions_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != Color || row < 0 || row >= static_cast<int>(table_.factions.size()))
            return;
        engine::Faction& f = table_.factions[static_cast<std::size_t>(row)];
        const QColor c = QColorDialog::getColor(toQColor(f.color), this, tr("Faction colour"));
        if (!c.isValid())
            return;
        f.color = {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF())};
        refresh();
        edited();
    });
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString id = QInputDialog::getText(this, tr("Add faction"), tr("Id (e.g. scientist):"), QLineEdit::Normal, QString(), &ok)
                               .trimmed();
        if (!ok || id.isEmpty())
            return;
        if (id == "player" || table_.find(id.toStdString())) {
            QMessageBox::warning(this, tr("Factions"), tr("A faction %1 exists already.").arg(id));
            return;
        }
        table_.factions.push_back({id.toStdString(), id.toStdString(), glm::vec3(0.7f)});
        refresh();
        edited();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = factions_->currentRow();
        if (row < 0 || row >= static_cast<int>(table_.factions.size()))
            return;
        const std::string id = table_.factions[static_cast<std::size_t>(row)].id;
        if (QMessageBox::question(this, tr("Remove faction"),
                                  tr("Remove %1? NPCs that belong to it become nobody's friend or foe.").arg(QString::fromStdString(id))) !=
            QMessageBox::Yes)
            return;
        table_.factions.erase(table_.factions.begin() + row);
        for (auto it = table_.relations.begin(); it != table_.relations.end();)
            it = it->first.first == id || it->first.second == id ? table_.relations.erase(it) : std::next(it);
        refresh();
        edited();
    });
    connect(matrix_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_)
            return;
        const int r = item->row(), c = item->column();
        const int n = static_cast<int>(table_.factions.size());
        if (r < 0 || c < 0 || r >= n || c >= n)
            return;
        bool ok = false;
        const double v = item->text().toDouble(&ok);
        if (ok)
            table_.setRelation(table_.factions[static_cast<std::size_t>(r)].id, table_.factions[static_cast<std::size_t>(c)].id,
                               static_cast<float>(std::round(v)));
        refreshMatrix();
        if (ok)
            edited();
    });
}

bool FactionsEditor::load(const QString& path, QString* error)
{
    std::string err;
    engine::FactionTable t;
    if (QFileInfo::exists(path)) {
        if (!engine::loadFactions(t, std::filesystem::path(path.toStdU16String()), &err)) {
            if (error)
                *error = QString::fromStdString(err);
            return false;
        }
    } else {
        t = engine::defaultFactions(); // saved on the first save
    }
    path_ = QFileInfo(path).absoluteFilePath();
    table_ = std::move(t);
    modified_ = false;
    refresh();
    return true;
}

bool FactionsEditor::save(QString* error)
{
    std::string err;
    if (!engine::saveFactions(table_, std::filesystem::path(path_.toStdU16String()), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    modified_ = false;
    emit modificationChanged();
    emit saved();
    return true;
}

QString FactionsEditor::title() const { return tr("Factions") + (modified_ ? QStringLiteral("*") : QString()); }

void FactionsEditor::edited()
{
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
}

void FactionsEditor::renameFaction(const std::string& from, const std::string& to)
{
    if (from == to)
        return;
    std::map<std::pair<std::string, std::string>, float> relations;
    for (const auto& [k, v] : table_.relations) {
        const std::string a = k.first == from ? to : k.first, b = k.second == from ? to : k.second;
        relations[a < b ? std::make_pair(a, b) : std::make_pair(b, a)] = v;
    }
    table_.relations = std::move(relations);
    for (engine::Faction& f : table_.factions)
        if (f.id == from)
            f.id = to;
    if (table_.playerFaction == from)
        table_.playerFaction = to;
}

void FactionsEditor::refresh()
{
    updating_ = true;
    player_->clear();
    for (const engine::Faction& f : table_.factions)
        player_->addItem(QString::fromStdString(f.name + " (" + f.id + ")"), QString::fromStdString(f.id));
    player_->setCurrentIndex(player_->findData(QString::fromStdString(table_.playerFaction)));
    hostile_->setValue(table_.hostileBelow);
    friendly_->setValue(table_.friendlyAbove);
    const int current = factions_->currentRow();
    factions_->setRowCount(static_cast<int>(table_.factions.size()));
    for (int i = 0; i < factions_->rowCount(); ++i) {
        const engine::Faction& f = table_.factions[static_cast<std::size_t>(i)];
        factions_->setItem(i, Id, new QTableWidgetItem(QString::fromStdString(f.id)));
        factions_->setItem(i, Name, new QTableWidgetItem(QString::fromStdString(f.name)));
        auto* color = new QTableWidgetItem;
        color->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        color->setBackground(toQColor(f.color));
        factions_->setItem(i, Color, color);
    }
    if (current >= 0 && current < factions_->rowCount())
        factions_->selectRow(current);
    updating_ = false;
    refreshMatrix();
}

void FactionsEditor::refreshMatrix()
{
    updating_ = true;
    const int n = static_cast<int>(table_.factions.size());
    matrix_->setRowCount(n);
    matrix_->setColumnCount(n);
    QStringList names;
    for (const engine::Faction& f : table_.factions)
        names << QString::fromStdString(f.name);
    matrix_->setHorizontalHeaderLabels(names);
    matrix_->setVerticalHeaderLabels(names);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const std::string& a = table_.factions[static_cast<std::size_t>(r)].id;
            const std::string& b = table_.factions[static_cast<std::size_t>(c)].id;
            const float v = table_.relation(a, b);
            auto* item = new QTableWidgetItem(QString::number(static_cast<int>(std::round(v))));
            item->setTextAlignment(Qt::AlignCenter);
            item->setBackground(attitudeColor(table_.attitudeOf(v)));
            item->setForeground(QColor(235, 235, 235));
            item->setToolTip(tr("%1 and %2: %3 (%4)%5")
                                 .arg(names[r], names[c])
                                 .arg(static_cast<int>(std::round(v)))
                                 .arg(QString::fromUtf8(engine::attitudeName(table_.attitudeOf(v))))
                                 .arg(table_.hasRelation(a, b) ? QString() : tr(", default")));
            matrix_->setItem(r, c, item);
        }
    matrix_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    updating_ = false;
}
