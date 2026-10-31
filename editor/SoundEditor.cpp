#include "SoundEditor.h"

#include "AssetPaths.h"
#include "EditorUi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

namespace {

std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdU16String()); }
QString fromPath(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }

const QString kSoundFilter = QObject::tr("Sounds (*.sound *.wav *.ogg *.mp3 *.flac);;All files (*)");
const QString kAudioFilter = QObject::tr("Audio files (*.wav *.ogg *.mp3 *.flac);;All files (*)");

QString shortPath(const QString& absolute)
{
    if (absolute.isEmpty())
        return {};
    const QString rel = QDir(assetRoot()).relativeFilePath(absolute);
    return rel.startsWith("..") ? absolute : rel;
}

engine::AudioSystem::Handle g_preview = 0;

} // namespace

engine::AudioSystem& editorAudio()
{
    static std::unique_ptr<engine::AudioSystem> audio;
    if (!audio) {
        audio = std::make_unique<engine::AudioSystem>();
        std::string error;
        if (!audio->init(&error))
            qWarning("%s", error.c_str());
        audio->setListener({0, 0, 0}, {0, 0, -1});
        // Finished previews are freed (and, without a device, mixed) on the GUI thread.
        auto* timer = new QTimer(QCoreApplication::instance());
        QObject::connect(timer, &QTimer::timeout, [] { audio->update(0.05f); });
        timer->start(50);
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] { audio.reset(); });
    }
    return *audio;
}

void previewSound(const QString& path)
{
    engine::AudioSystem& audio = editorAudio();
    audio.stop(g_preview);
    std::string error;
    if (auto def = audio.loadSound(toPath(path), &error))
        g_preview = audio.play2D(*def);
    else
        qWarning("%s", error.c_str());
}

void stopSoundPreview()
{
    editorAudio().stop(g_preview, 0.1f);
    g_preview = 0;
}

// ------------------------------------------------------------------ picker

SoundPicker::SoundPicker(QWidget* parent) : QWidget(parent)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    edit_ = new QLineEdit(this);
    edit_->setReadOnly(true);
    edit_->setPlaceholderText(tr("none"));
    auto* browse = new QToolButton(this);
    browse->setText(tr("..."));
    browse->setToolTip(tr("Choose a .sound or audio file"));
    auto* play = new QToolButton(this);
    play->setText(QStringLiteral("▶"));
    play->setToolTip(tr("Preview"));
    auto* clear = new QToolButton(this);
    clear->setText(QStringLiteral("✕"));
    clear->setToolTip(tr("No sound"));
    row->addWidget(edit_, 1);
    row->addWidget(browse);
    row->addWidget(play);
    row->addWidget(clear);
    connect(browse, &QToolButton::clicked, this, [this] {
        const QString start = path_.isEmpty() ? assetPath("sounds") : QFileInfo(path_).absolutePath();
        const QString file = QFileDialog::getOpenFileName(this, tr("Sound"), start, kSoundFilter);
        if (file.isEmpty())
            return;
        path_ = QFileInfo(file).absoluteFilePath();
        updateText();
        emit pathChanged(path_);
    });
    connect(play, &QToolButton::clicked, this, [this] {
        if (!path_.isEmpty())
            previewSound(path_);
    });
    connect(clear, &QToolButton::clicked, this, [this] {
        if (path_.isEmpty())
            return;
        path_.clear();
        updateText();
        emit pathChanged(path_);
    });
}

void SoundPicker::setPath(const QString& absolutePath)
{
    path_ = absolutePath;
    updateText();
}

void SoundPicker::updateText()
{
    edit_->setText(shortPath(path_));
    edit_->setToolTip(path_);
}

// ------------------------------------------------------------------ editor

SoundEditor::SoundEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    files_ = new QListWidget(this);
    files_->setMaximumHeight(140);
    files_->setToolTip(tr("Variations: each play picks one at random (never the same twice in a row). Double-click to hear one."));
    auto* fileButtons = new QHBoxLayout();
    auto* addFile = new QPushButton(tr("Add..."), this);
    auto* removeFile = new QPushButton(tr("Remove"), this);
    fileButtons->addWidget(addFile);
    fileButtons->addWidget(removeFile);
    fileButtons->addStretch(1);

    auto spin = [this](double min, double max, double step, const QString& suffix, int decimals = 2) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setKeyboardTracking(false);
        connect(s, &QDoubleSpinBox::valueChanged, this, &SoundEditor::edited);
        return s;
    };
    volume_ = spin(0, 4, 0.05, QString());
    volume_->setToolTip(tr("Linear gain (1 = as recorded)"));
    pitchMin_ = spin(0.1, 4, 0.01, QString());
    pitchMax_ = spin(0.1, 4, 0.01, QString());
    auto* pitchRow = new QHBoxLayout();
    pitchRow->addWidget(pitchMin_);
    pitchRow->addWidget(new QLabel(tr("to"), this));
    pitchRow->addWidget(pitchMax_);
    spatial_ = new QCheckBox(tr("3D (positioned in the world)"), this);
    minDistance_ = spin(0.01, 1000, 0.5, tr(" m"));
    minDistance_->setToolTip(tr("Full volume up to this distance"));
    maxDistance_ = spin(0.1, 5000, 5, tr(" m"));
    maxDistance_->setToolTip(tr("No further attenuation beyond this distance"));
    rolloff_ = spin(0, 10, 0.1, QString());
    rolloff_->setToolTip(tr("How fast the sound fades with distance (inverse distance)"));
    loop_ = new QCheckBox(tr("Loop"), this);
    stream_ = new QCheckBox(tr("Stream from disk (music, long loops)"), this);
    group_ = new QComboBox(this);
    for (int g = 0; g < static_cast<int>(engine::AudioGroup::Count); ++g)
        group_->addItem(QString::fromLatin1(engine::audioGroupName(static_cast<engine::AudioGroup>(g))), g);
    maxInstances_ = new QSpinBox(this);
    maxInstances_->setRange(1, 64);
    maxInstances_->setToolTip(tr("Plays at once; another play stops the oldest"));
    connect(maxInstances_, &QSpinBox::valueChanged, this, &SoundEditor::edited);
    for (QCheckBox* c : {spatial_, loop_, stream_})
        connect(c, &QCheckBox::toggled, this, &SoundEditor::edited);
    connect(group_, &QComboBox::currentIndexChanged, this, &SoundEditor::edited);

    auto* preview = new QHBoxLayout();
    auto* play = new QPushButton(tr("▶ Play"), this);
    auto* stop = new QPushButton(tr("■ Stop"), this);
    auto* distance = new QDoubleSpinBox(this);
    distance->setRange(0, 1000);
    distance->setSingleStep(5);
    distance->setSuffix(tr(" m away"));
    distance->setToolTip(tr("Preview a 3D sound from this distance (in front of you)"));
    distance->setObjectName("previewDistance");
    preview->addWidget(play);
    preview->addWidget(stop);
    preview->addWidget(distance);
    preview->addStretch(1);
    auto* save = new QPushButton(tr("Save"), this);
    preview->addWidget(save);

    form->addRow(tr("Variations"), files_);
    form->addRow(QString(), fileButtons);
    form->addRow(tr("Volume"), volume_);
    form->addRow(tr("Random pitch"), pitchRow);
    form->addRow(QString(), spatial_);
    form->addRow(tr("Full volume within"), minDistance_);
    form->addRow(tr("Fades out until"), maxDistance_);
    form->addRow(tr("Rolloff"), rolloff_);
    form->addRow(QString(), loop_);
    form->addRow(QString(), stream_);
    form->addRow(tr("Mixer group"), group_);
    form->addRow(tr("Max instances"), maxInstances_);
    info_ = new QLabel(this);
    info_->setObjectName("dim");
    info_->setWordWrap(true);
    layout->addLayout(preview);
    layout->addLayout(form);
    layout->addWidget(info_);
    layout->addStretch(1);

    connect(addFile, &QPushButton::clicked, this, [this] {
        const QStringList picked = QFileDialog::getOpenFileNames(this, tr("Add variations"), QFileInfo(path_).absolutePath(), kAudioFilter);
        for (const QString& f : picked)
            sound_.files.push_back(toPath(QFileInfo(f).absoluteFilePath()).lexically_normal());
        if (!picked.isEmpty()) {
            showSound();
            edited();
        }
    });
    connect(removeFile, &QPushButton::clicked, this, [this] {
        const int row = files_->currentRow();
        if (row < 0 || row >= static_cast<int>(sound_.files.size()))
            return;
        sound_.files.erase(sound_.files.begin() + row);
        showSound();
        edited();
    });
    connect(files_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        previewSound(item->data(Qt::UserRole).toString());
    });
    connect(play, &QPushButton::clicked, this, &SoundEditor::play);
    connect(stop, &QPushButton::clicked, this, [this] {
        editorAudio().stop(preview_, 0.15f);
        stopSoundPreview();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        QString error;
        if (!this->save(&error))
            info_->setText(error);
    });
}

bool SoundEditor::load(const QString& path, QString* error)
{
    std::string err;
    engine::SoundDef def;
    if (!engine::loadSoundFile(def, toPath(path), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    sound_ = std::move(def);
    modified_ = false;
    showSound();
    return true;
}

bool SoundEditor::save(QString* error)
{
    std::string err;
    if (!engine::saveSoundFile(sound_, toPath(path_), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    modified_ = false;
    emit modificationChanged();
    return true;
}

QString SoundEditor::title() const
{
    return QFileInfo(path_).fileName() + (modified_ ? "*" : "");
}

void SoundEditor::showSound()
{
    updating_ = true;
    files_->clear();
    for (const auto& f : sound_.files) {
        auto* item = new QListWidgetItem(QString::fromStdU16String(f.filename().u16string()), files_);
        item->setData(Qt::UserRole, fromPath(f));
        item->setToolTip(fromPath(f));
    }
    volume_->setValue(sound_.volume);
    pitchMin_->setValue(sound_.pitchMin);
    pitchMax_->setValue(sound_.pitchMax);
    spatial_->setChecked(sound_.spatial);
    minDistance_->setValue(sound_.minDistance);
    maxDistance_->setValue(sound_.maxDistance);
    rolloff_->setValue(sound_.rolloff);
    loop_->setChecked(sound_.loop);
    stream_->setChecked(sound_.stream);
    group_->setCurrentIndex(static_cast<int>(sound_.group));
    maxInstances_->setValue(sound_.maxInstances);
    for (QWidget* w : std::initializer_list<QWidget*>{minDistance_, maxDistance_, rolloff_})
        w->setEnabled(sound_.spatial);
    info_->setText(sound_.files.empty() ? tr("No variations yet: add .wav / .ogg files.")
                                        : tr("%n variation(s)", nullptr, static_cast<int>(sound_.files.size())));
    updating_ = false;
}

void SoundEditor::edited()
{
    if (updating_)
        return;
    sound_.volume = static_cast<float>(volume_->value());
    sound_.pitchMin = static_cast<float>(pitchMin_->value());
    sound_.pitchMax = std::max(sound_.pitchMin, static_cast<float>(pitchMax_->value()));
    sound_.spatial = spatial_->isChecked();
    sound_.minDistance = static_cast<float>(minDistance_->value());
    sound_.maxDistance = std::max(sound_.minDistance, static_cast<float>(maxDistance_->value()));
    sound_.rolloff = static_cast<float>(rolloff_->value());
    sound_.loop = loop_->isChecked();
    sound_.stream = stream_->isChecked();
    sound_.group = static_cast<engine::AudioGroup>(group_->currentIndex());
    sound_.maxInstances = maxInstances_->value();
    for (QWidget* w : std::initializer_list<QWidget*>{minDistance_, maxDistance_, rolloff_})
        w->setEnabled(sound_.spatial);
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
}

void SoundEditor::play()
{
    engine::AudioSystem& audio = editorAudio();
    audio.stop(preview_);
    const auto* distance = findChild<QDoubleSpinBox*>("previewDistance");
    const float d = distance ? static_cast<float>(distance->value()) : 0.0f;
    // The edited (maybe unsaved) settings play as they are; the listener is at the origin looking down -Z.
    preview_ = sound_.spatial ? audio.play(sound_, {0.0f, 0.0f, -std::max(d, 0.01f)}) : audio.play2D(sound_);
}
