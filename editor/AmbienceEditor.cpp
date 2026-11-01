#include "AmbienceEditor.h"

#include "SoundEditor.h"

#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox* spin(QWidget* parent, double min, double max, double step, int decimals, double value)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setValue(value);
    s->setKeyboardTracking(false);
    s->setFrame(false);
    return s;
}

QTableWidget* table(QWidget* parent, const QStringList& headers, const QStringList& tips)
{
    auto* t = new QTableWidget(0, static_cast<int>(headers.size()), parent);
    t->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < headers.size() && i < tips.size(); ++i)
        t->horizontalHeaderItem(i)->setToolTip(tips[i]);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < headers.size(); ++i)
        t->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    return t;
}

std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdU16String()); }
QString fromPath(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }

} // namespace

AmbienceEditor::AmbienceEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(tr("The sound of a place. <b>Layers</b> loop all the time (wind, drone, birds), scaled by day and by night. "
                               "<b>Random sounds</b> play now and then somewhere around the player: every Interval seconds a try that "
                               "succeeds with the Day / Night chance, at a random direction and Distance. Use it in Environment > Sound "
                               "> Ambience or in an Ambience zone."),
                            this);
    help->setWordWrap(true);
    help->setObjectName("dim");
    layout->addWidget(help);

    layout->addWidget(new QLabel(tr("<b>Layers</b>"), this));
    layers_ = table(this, {tr("Sound"), tr("Volume"), tr("Day"), tr("Night")},
                    {tr("Looping .sound (2D)"), tr("Volume of the layer"), tr("Multiplier by day"), tr("Multiplier by night")});
    layout->addWidget(layers_, 1);
    auto* layerButtons = new QHBoxLayout;
    auto* addLayer = new QPushButton(tr("Add layer"), this);
    auto* removeLayer = new QPushButton(tr("Remove"), this);
    layerButtons->addWidget(addLayer);
    layerButtons->addWidget(removeLayer);
    layerButtons->addStretch(1);
    preview_ = new QPushButton(tr("▶ Preview layers"), this);
    preview_->setCheckable(true);
    layerButtons->addWidget(preview_);
    layout->addLayout(layerButtons);

    layout->addWidget(new QLabel(tr("<b>Random sounds</b>"), this));
    randoms_ = table(this,
                     {tr("Sound"), tr("Every min s"), tr("max s"), tr("Distance min m"), tr("max m"), tr("Day"), tr("Night"), tr("Height m"),
                      tr("Duck")},
                     {tr("3D .sound played at a random place around the player"), tr("Shortest time between tries"),
                      tr("Longest time between tries"), tr("Nearest distance from the player"), tr("Farthest distance"),
                      tr("Chance that a try plays by day (0..1)"), tr("Chance by night"), tr("Height above the player (birds, roofs)"),
                      tr("How much the ambience steps back when it plays (0..1), e.g. a far explosion")});
    layout->addWidget(randoms_, 1);
    auto* randomButtons = new QHBoxLayout;
    auto* addRandom = new QPushButton(tr("Add random sound"), this);
    auto* removeRandom = new QPushButton(tr("Remove"), this);
    auto* playRandom = new QPushButton(tr("▶ Play selected (at its distance)"), this);
    randomButtons->addWidget(addRandom);
    randomButtons->addWidget(removeRandom);
    randomButtons->addStretch(1);
    randomButtons->addWidget(playRandom);
    layout->addLayout(randomButtons);

    connect(addLayer, &QPushButton::clicked, this, [this] {
        set_.layers.push_back({});
        rebuild();
        edited();
    });
    connect(removeLayer, &QPushButton::clicked, this, [this] {
        const int row = layers_->currentRow();
        if (row >= 0 && row < static_cast<int>(set_.layers.size())) {
            set_.layers.erase(set_.layers.begin() + row);
            rebuild();
            edited();
        }
    });
    connect(addRandom, &QPushButton::clicked, this, [this] {
        set_.randoms.push_back({});
        rebuild();
        edited();
    });
    connect(removeRandom, &QPushButton::clicked, this, [this] {
        const int row = randoms_->currentRow();
        if (row >= 0 && row < static_cast<int>(set_.randoms.size())) {
            set_.randoms.erase(set_.randoms.begin() + row);
            rebuild();
            edited();
        }
    });
    connect(preview_, &QPushButton::toggled, this, [this] { togglePreview(); });
    connect(playRandom, &QPushButton::clicked, this, &AmbienceEditor::playRandom);
}

AmbienceEditor::~AmbienceEditor() { stopPreview(); }

bool AmbienceEditor::load(const QString& path, QString* error)
{
    std::string e;
    engine::AmbienceSet set;
    if (!engine::loadAmbienceFile(set, toPath(path), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    set_ = std::move(set);
    modified_ = false;
    rebuild();
    return true;
}

bool AmbienceEditor::save(QString* error)
{
    std::string e;
    if (!engine::saveAmbienceFile(set_, toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    modified_ = false;
    emit modificationChanged();
    return true;
}

QString AmbienceEditor::title() const { return QFileInfo(path_).fileName() + (modified_ ? QStringLiteral("*") : QString()); }

void AmbienceEditor::edited()
{
    if (updating_)
        return;
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
    if (preview_->isChecked()) { // hear the change
        stopPreview();
        togglePreview();
    }
}

void AmbienceEditor::rebuild()
{
    updating_ = true;
    layers_->setRowCount(static_cast<int>(set_.layers.size()));
    for (int r = 0; r < layers_->rowCount(); ++r) {
        engine::AmbienceLayer& l = set_.layers[static_cast<std::size_t>(r)];
        auto* sound = new SoundPicker(layers_);
        sound->setPath(fromPath(l.sound));
        connect(sound, &SoundPicker::pathChanged, this, [this, r](const QString& p) {
            set_.layers[static_cast<std::size_t>(r)].sound = toPath(p);
            edited();
        });
        layers_->setCellWidget(r, 0, sound);
        float engine::AmbienceLayer::*fields[] = {&engine::AmbienceLayer::volume, &engine::AmbienceLayer::day, &engine::AmbienceLayer::night};
        for (int c = 0; c < 3; ++c) {
            auto* s = spin(layers_, 0.0, c == 0 ? 2.0 : 1.0, 0.05, 2, l.*fields[c]);
            const auto field = fields[c];
            connect(s, &QDoubleSpinBox::valueChanged, this, [this, r, field](double v) {
                set_.layers[static_cast<std::size_t>(r)].*field = static_cast<float>(v);
                edited();
            });
            layers_->setCellWidget(r, c + 1, s);
        }
    }
    randoms_->setRowCount(static_cast<int>(set_.randoms.size()));
    for (int r = 0; r < randoms_->rowCount(); ++r) {
        engine::AmbienceRandom& x = set_.randoms[static_cast<std::size_t>(r)];
        auto* sound = new SoundPicker(randoms_);
        sound->setPath(fromPath(x.sound));
        connect(sound, &SoundPicker::pathChanged, this, [this, r](const QString& p) {
            set_.randoms[static_cast<std::size_t>(r)].sound = toPath(p);
            edited();
        });
        randoms_->setCellWidget(r, 0, sound);
        struct Field {
            float engine::AmbienceRandom::*member;
            double max, step;
            int decimals;
        };
        const Field fields[] = {{&engine::AmbienceRandom::minInterval, 3600, 1, 1}, {&engine::AmbienceRandom::maxInterval, 3600, 1, 1},
                                {&engine::AmbienceRandom::minDistance, 2000, 5, 0}, {&engine::AmbienceRandom::maxDistance, 2000, 5, 0},
                                {&engine::AmbienceRandom::day, 1, 0.05, 2},         {&engine::AmbienceRandom::night, 1, 0.05, 2},
                                {&engine::AmbienceRandom::height, 200, 1, 1},       {&engine::AmbienceRandom::duck, 1, 0.05, 2}};
        for (int c = 0; c < 8; ++c) {
            auto* s = spin(randoms_, 0.0, fields[c].max, fields[c].step, fields[c].decimals, x.*fields[c].member);
            const auto member = fields[c].member;
            connect(s, &QDoubleSpinBox::valueChanged, this, [this, r, member](double v) {
                set_.randoms[static_cast<std::size_t>(r)].*member = static_cast<float>(v);
                edited();
            });
            randoms_->setCellWidget(r, c + 1, s);
        }
    }
    updating_ = false;
}

void AmbienceEditor::stopPreview()
{
    for (engine::AudioSystem::Handle h : playing_)
        editorAudio().stop(h, 0.3f);
    playing_.clear();
}

void AmbienceEditor::togglePreview()
{
    stopPreview();
    if (!preview_->isChecked())
        return;
    for (const engine::AmbienceLayer& l : set_.layers) {
        auto def = editorAudio().loadSound(l.sound);
        if (def)
            playing_.push_back(editorAudio().play2D(*def, engine::ambienceLayerVolume(l, 0.0f)));
    }
}

void AmbienceEditor::playRandom()
{
    const int row = randoms_->currentRow();
    if (row < 0 || row >= static_cast<int>(set_.randoms.size()))
        return;
    const engine::AmbienceRandom& r = set_.randoms[static_cast<std::size_t>(row)];
    if (auto def = editorAudio().loadSound(r.sound)) {
        // In front of the listener at the middle of its distance range.
        const glm::vec3 ear = editorAudio().listenerPosition();
        editorAudio().play(*def, ear + glm::vec3(0.0f, r.height, -(r.minDistance + r.maxDistance) * 0.5f));
    }
}
