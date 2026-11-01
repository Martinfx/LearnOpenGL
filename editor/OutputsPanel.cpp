#include "OutputsPanel.h"

#include <engine/EntityIO.h>

#include <QCheckBox>
#include <QFile>
#include <QRegularExpression>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace {

enum Column { ColOutput, ColTarget, ColInput, ColParameter, ColDelay, ColOnce, ColCount };

QComboBox* editableCombo(QWidget* parent)
{
    auto* c = new QComboBox(parent);
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(8);
    return c;
}

// Names a script adds: its fire_output("X") calls (outputs) or `input == "X"` checks (inputs).
std::vector<engine::IoName> scriptNames(const std::filesystem::path& script, bool outputs)
{
    static std::map<std::pair<std::filesystem::path, bool>, std::vector<std::string>> cache; // per editor session
    std::vector<engine::IoName> out;
    if (script.empty())
        return out;
    auto& names = cache[{script, outputs}];
    if (names.empty()) {
        QFile file(QString::fromStdU16String(script.u16string()));
        if (file.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(file.readAll());
            static const QRegularExpression outRe(QStringLiteral(R"re(fire_output\s*\(\s*"([A-Za-z0-9_]+)")re"));
            static const QRegularExpression inRe(QStringLiteral(R"re(input\s*==\s*"([A-Za-z0-9_]+)")re"));
            for (auto it = (outputs ? outRe : inRe).globalMatch(text); it.hasNext();) {
                const std::string n = it.next().captured(1).toStdString();
                if (std::find(names.begin(), names.end(), n) == names.end())
                    names.push_back(n);
            }
        }
        if (names.empty())
            names.push_back(std::string()); // marks "read, none"
    }
    for (const std::string& n : names)
        if (!n.empty())
            out.push_back({n.c_str(), outputs ? "Output of the entity's script" : "Handled by the entity's script (on_input)"});
    return out;
}

void setItems(QComboBox* combo, const std::vector<engine::IoName>& names, const QString& current)
{
    combo->clear();
    for (const engine::IoName& n : names) {
        combo->addItem(QString::fromUtf8(n.name));
        combo->setItemData(combo->count() - 1, QString::fromUtf8(n.help), Qt::ToolTipRole);
    }
    combo->setEditText(current);
}

} // namespace

OutputsPanel::OutputsPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    {
        triggerRow_ = new QWidget(this);
        auto* row = new QHBoxLayout(triggerRow_);
        row->setContentsMargins(0, 0, 0, 0);
        filter_ = new QComboBox(triggerRow_);
        filter_->addItem(tr("Player"), QStringLiteral("player"));
        filter_->addItem(tr("Characters (NPC)"), QStringLiteral("npc"));
        filter_->addItem(tr("Player and characters"), QStringLiteral("any"));
        filter_->setToolTip(tr("Who sets the trigger off when entering its volume (the entity size)"));
        triggerOnce_ = new QCheckBox(tr("Once"), triggerRow_);
        triggerOnce_->setToolTip(tr("Switches itself off after the first OnTrigger (Enable input turns it on again)"));
        row->addWidget(new QLabel(tr("Reacts to"), triggerRow_));
        row->addWidget(filter_, 1);
        row->addWidget(triggerOnce_);
        layout->addWidget(triggerRow_);
        startEnabled_ = new QCheckBox(tr("Starts enabled (Enable / Disable / Toggle inputs switch it)"), this);
        layout->addWidget(startEnabled_);
        auto setProperty = [this](const char* key, const std::string& value, const std::string& defaultValue) {
            if (updating_ || !entity_)
                return;
            const auto it = entity_->properties.find(key);
            const std::string current = it == entity_->properties.end() ? defaultValue : it->second;
            if (current == value)
                return;
            if (value == defaultValue)
                entity_->properties.erase(key);
            else
                entity_->properties[key] = value;
            emit edited();
        };
        connect(filter_, &QComboBox::currentIndexChanged, this,
                [this, setProperty](int) { setProperty("filter", filter_->currentData().toString().toStdString(), "player"); });
        connect(triggerOnce_, &QCheckBox::toggled, this, [setProperty](bool on) { setProperty("once", on ? "1" : "0", "0"); });
        connect(startEnabled_, &QCheckBox::toggled, this, [setProperty](bool on) { setProperty("enabled", on ? "1" : "0", "1"); });
    }
    table_ = new QTableWidget(0, ColCount, this);
    table_->setHorizontalHeaderLabels({tr("Output"), tr("Target"), tr("Input"), tr("Parameter"), tr("Delay"), tr("Once")});
    table_->horizontalHeaderItem(ColOutput)->setToolTip(tr("When this entity does this..."));
    table_->horizontalHeaderItem(ColTarget)->setToolTip(tr("...every entity with this name (or !self, !activator, !player)..."));
    table_->horizontalHeaderItem(ColInput)->setToolTip(tr("...receives this input"));
    table_->horizontalHeaderItem(ColParameter)->setToolTip(tr("Optional value for the input (message text, damage, key=value)"));
    table_->horizontalHeaderItem(ColDelay)->setToolTip(tr("Seconds before the input arrives"));
    table_->horizontalHeaderItem(ColOnce)->setToolTip(tr("Only the first time the output fires"));
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setColumnWidth(ColOutput, 110);
    table_->setColumnWidth(ColTarget, 110);
    table_->setColumnWidth(ColInput, 100);
    table_->setColumnWidth(ColParameter, 90);
    table_->setColumnWidth(ColDelay, 64);
    table_->setColumnWidth(ColOnce, 44);
    table_->setMinimumHeight(140);
    layout->addWidget(table_);

    auto* buttons = new QHBoxLayout;
    add_ = new QPushButton(tr("Add"), this);
    add_->setToolTip(tr("New connection from this entity"));
    remove_ = new QPushButton(tr("Remove"), this);
    pick_ = new QPushButton(tr("Pick target"), this);
    pick_->setToolTip(tr("Then click the target entity in the viewport (Esc cancels)"));
    buttons->addWidget(add_);
    buttons->addWidget(remove_);
    buttons->addStretch(1);
    buttons->addWidget(pick_);
    layout->addLayout(buttons);
    incoming_ = new QLabel(this);
    incoming_->setWordWrap(true);
    incoming_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(incoming_, &QLabel::linkActivated, this, [this](const QString& name) { emit selectEntityRequested(name); });
    layout->addWidget(incoming_);

    connect(add_, &QPushButton::clicked, this, &OutputsPanel::addConnection);
    connect(remove_, &QPushButton::clicked, this, &OutputsPanel::removeConnection);
    connect(pick_, &QPushButton::clicked, this, [this] {
        if (entity_)
            emit pickTargetRequested();
    });
    setEntity(nullptr, nullptr);
}

void OutputsPanel::setEntity(engine::Entity* entity, const engine::Scene* scene)
{
    entity_ = entity;
    scene_ = scene;
    refresh();
}

void OutputsPanel::refresh()
{
    updating_ = true;
    const int keepRow = table_->currentRow();
    table_->setRowCount(entity_ ? static_cast<int>(entity_->outputs.size()) : 0);
    for (int r = 0; r < table_->rowCount(); ++r)
        rebuildRow(r);
    if (keepRow >= 0 && keepRow < table_->rowCount())
        table_->selectRow(keepRow);
    add_->setEnabled(entity_ != nullptr);
    remove_->setEnabled(entity_ && !entity_->outputs.empty());
    pick_->setEnabled(entity_ != nullptr);
    const std::string type = entity_ ? entity_->type : std::string();
    triggerRow_->setVisible(type == "trigger");
    startEnabled_->setVisible(type == "trigger" || type == "relay" || type == "sound" || type == "particle");
    if (entity_) {
        const auto prop = [this](const char* key, const char* fallback) {
            const auto it = entity_->properties.find(key);
            return QString::fromStdString(it == entity_->properties.end() ? std::string(fallback) : it->second);
        };
        filter_->setCurrentIndex(std::max(0, filter_->findData(prop("filter", "player"))));
        triggerOnce_->setChecked(prop("once", "0") == "1");
        startEnabled_->setChecked(prop("enabled", "1") != "0");
    }
    updateIncoming();
    updating_ = false;
}

void OutputsPanel::rebuildRow(int row)
{
    const engine::EntityOutput& o = entity_->outputs[static_cast<std::size_t>(row)];
    auto* output = editableCombo(table_);
    {
        auto outputs = scriptNames(entity_->scriptPath, true);
        for (const engine::IoName& n : engine::entityOutputs(entity_->type))
            outputs.push_back(n);
        setItems(output, outputs, QString::fromStdString(o.output));
    }

    auto* target = editableCombo(table_);
    target->addItems({"!self", "!activator", "!player"});
    QStringList names;
    for (const engine::Entity& e : scene_->entities)
        if (!names.contains(QString::fromStdString(e.name)))
            names.push_back(QString::fromStdString(e.name));
    names.sort(Qt::CaseInsensitive);
    target->addItems(names);
    target->setEditText(QString::fromStdString(o.target));
    // The inputs the target understands (the first entity of that name).
    std::string targetType;
    std::filesystem::path targetScript;
    if (o.target == "!self") {
        targetType = entity_->type;
        targetScript = entity_->scriptPath;
    } else if (const auto found = engine::findEntitiesByName(*scene_, o.target); !found.empty()) {
        targetType = scene_->entities[static_cast<std::size_t>(found.front())].type;
        targetScript = scene_->entities[static_cast<std::size_t>(found.front())].scriptPath;
    }
    const bool missing = !o.target.empty() && o.target[0] != '!' && engine::findEntitiesByName(*scene_, o.target).empty();
    target->setStyleSheet(missing ? QStringLiteral("QComboBox { color: #ff6b5b; }") : QString());
    target->setToolTip(missing ? tr("No entity has this name") : tr("Target: %1 (%2)").arg(QString::fromStdString(o.target), QString::fromStdString(targetType)));

    auto* input = editableCombo(table_);
    {
        auto inputs = scriptNames(targetScript, false);
        for (const engine::IoName& n : engine::entityInputs(targetType))
            inputs.push_back(n);
        setItems(input, inputs, QString::fromStdString(o.input));
    }
    if (o.target == "!player") {
        input->clear();
        input->addItems({"Damage", "Heal", "ShowMessage"});
        input->setEditText(QString::fromStdString(o.input));
    }

    auto* parameter = new QLineEdit(QString::fromStdString(o.parameter), table_);
    auto* delay = new QDoubleSpinBox(table_);
    delay->setRange(0.0, 3600.0);
    delay->setDecimals(2);
    delay->setSingleStep(0.5);
    delay->setValue(o.delay);
    delay->setKeyboardTracking(false);
    auto* onceBox = new QWidget(table_);
    auto* onceLayout = new QHBoxLayout(onceBox);
    onceLayout->setContentsMargins(0, 0, 0, 0);
    onceLayout->setAlignment(Qt::AlignCenter);
    auto* once = new QCheckBox(onceBox);
    once->setChecked(o.once);
    onceLayout->addWidget(once);

    table_->setCellWidget(row, ColOutput, output);
    table_->setCellWidget(row, ColTarget, target);
    table_->setCellWidget(row, ColInput, input);
    table_->setCellWidget(row, ColParameter, parameter);
    table_->setCellWidget(row, ColDelay, delay);
    table_->setCellWidget(row, ColOnce, onceBox);

    // Each field writes straight into the connection; a new target also refreshes the input list.
    auto field = [this, row]() -> engine::EntityOutput* {
        if (updating_ || !entity_ || row >= static_cast<int>(entity_->outputs.size()))
            return nullptr;
        table_->selectRow(row);
        return &entity_->outputs[static_cast<std::size_t>(row)];
    };
    auto textOf = [](QComboBox* c) { return c->currentText().trimmed().toStdString(); };
    connect(output, &QComboBox::currentTextChanged, this, [=, this] {
        if (auto* o2 = field(); o2 && o2->output != textOf(output)) {
            o2->output = textOf(output);
            emit edited();
        }
    });
    connect(target, &QComboBox::currentTextChanged, this, [=, this] {
        if (auto* o2 = field(); o2 && o2->target != textOf(target)) {
            o2->target = textOf(target);
            emit edited();
            QMetaObject::invokeMethod(this, [this, row] {
                if (entity_ && row < table_->rowCount()) {
                    updating_ = true;
                    rebuildRow(row);
                    updating_ = false;
                    updateIncoming();
                }
            }, Qt::QueuedConnection);
        }
    });
    connect(input, &QComboBox::currentTextChanged, this, [=, this] {
        if (auto* o2 = field(); o2 && o2->input != textOf(input)) {
            o2->input = textOf(input);
            emit edited();
        }
    });
    connect(parameter, &QLineEdit::editingFinished, this, [=, this] {
        if (auto* o2 = field(); o2 && o2->parameter != parameter->text().toStdString()) {
            o2->parameter = parameter->text().toStdString();
            emit edited();
        }
    });
    connect(delay, &QDoubleSpinBox::valueChanged, this, [=, this](double v) {
        if (auto* o2 = field(); o2 && o2->delay != static_cast<float>(v)) {
            o2->delay = static_cast<float>(v);
            emit edited();
        }
    });
    connect(once, &QCheckBox::toggled, this, [=, this](bool on) {
        if (auto* o2 = field(); o2 && o2->once != on) {
            o2->once = on;
            emit edited();
        }
    });
}

void OutputsPanel::addConnection()
{
    if (!entity_)
        return;
    engine::EntityOutput o;
    const auto outputs = engine::entityOutputs(entity_->type);
    o.output = outputs.empty() ? "OnStart" : outputs.front().name;
    o.input = "Trigger";
    if (!entity_->outputs.empty()) { // likely another target of the same event
        o.output = entity_->outputs.back().output;
        o.input = entity_->outputs.back().input;
    }
    entity_->outputs.push_back(o);
    refresh();
    table_->selectRow(table_->rowCount() - 1);
    emit edited();
}

void OutputsPanel::removeConnection()
{
    const int row = table_->currentRow();
    if (!entity_ || row < 0 || row >= static_cast<int>(entity_->outputs.size()))
        return;
    entity_->outputs.erase(entity_->outputs.begin() + row);
    refresh();
    emit edited();
}

void OutputsPanel::setPickedTarget(const QString& name)
{
    if (!entity_)
        return;
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entity_->outputs.size())) {
        addConnection();
        row = static_cast<int>(entity_->outputs.size()) - 1;
    }
    engine::EntityOutput& o = entity_->outputs[static_cast<std::size_t>(row)];
    o.target = name.toStdString();
    // A sensible input for what was picked.
    if (const auto found = engine::findEntitiesByName(*scene_, o.target); !found.empty()) {
        const std::string& type = scene_->entities[static_cast<std::size_t>(found.front())].type;
        const auto inputs = engine::entityInputs(type);
        const bool known = std::any_of(inputs.begin(), inputs.end(), [&](const engine::IoName& n) { return o.input == n.name; });
        if (!known && !inputs.empty())
            o.input = inputs.front().name;
    }
    refresh();
    table_->selectRow(row);
    emit edited();
}

void OutputsPanel::updateIncoming()
{
    if (!entity_ || !scene_) {
        incoming_->clear();
        return;
    }
    QStringList from;
    for (const engine::Entity& e : scene_->entities)
        for (const engine::EntityOutput& o : e.outputs)
            if (o.target == entity_->name && &e != entity_)
                from.push_back(tr("<a href=\"%1\">%1</a>.%2 → %3").arg(QString::fromStdString(e.name).toHtmlEscaped(),
                                                                         QString::fromStdString(o.output), QString::fromStdString(o.input)));
    incoming_->setText(from.isEmpty() ? tr("<i>No inputs from other entities</i>") : tr("Inputs from: ") + from.join(", "));
}
