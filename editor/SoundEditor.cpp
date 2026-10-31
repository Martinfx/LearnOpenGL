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
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <engine/JobSystem.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
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

// ------------------------------------------------------------------ views

namespace {

const QColor kBack(24, 25, 28);
const QColor kGrid(52, 54, 60);
const QColor kText(150, 154, 162);
const QColor kAccent(86, 156, 214);
const QColor kWarm(230, 160, 70);

// OpenAL's AL_INVERSE_DISTANCE_CLAMPED, as the game hears it.
float distanceGain(const engine::SoundDef& s, float d)
{
    if (!s.spatial)
        return 1.0f;
    const float ref = std::max(s.minDistance, 0.001f);
    d = std::clamp(d, ref, std::max(ref, s.maxDistance));
    return ref / (ref + s.rolloff * (d - ref));
}

} // namespace

// Min / max envelope of one decoded variation.
class WaveformView : public QWidget {
public:
    explicit WaveformView(QWidget* parent) : QWidget(parent) { setMinimumHeight(90); }
    // One min/max pair per column bucket (-1..1) for each channel.
    void setPeaks(std::vector<std::vector<std::pair<float, float>>> peaks, QString message = {})
    {
        peaks_ = std::move(peaks);
        message_ = std::move(message);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), kBack);
        if (peaks_.empty()) {
            p.setPen(kText);
            p.drawText(rect(), Qt::AlignCenter, message_.isEmpty() ? tr("Select a variation") : message_);
            return;
        }
        const int lanes = static_cast<int>(peaks_.size());
        const double laneH = height() / double(lanes);
        for (int c = 0; c < lanes; ++c) {
            const auto& lane = peaks_[c];
            const double mid = laneH * (c + 0.5);
            p.setPen(kGrid);
            p.drawLine(QPointF(0, mid), QPointF(width(), mid));
            if (lane.empty())
                continue;
            p.setPen(kAccent);
            for (int x = 0; x < width(); ++x) {
                const std::size_t i = std::min(lane.size() - 1, std::size_t(double(x) / width() * lane.size()));
                const auto [lo, hi] = lane[i];
                p.drawLine(QPointF(x + 0.5, mid - hi * laneH * 0.46), QPointF(x + 0.5, mid - lo * laneH * 0.46));
            }
        }
    }

private:
    std::vector<std::vector<std::pair<float, float>>> peaks_;
    QString message_;
};

// Gain over distance; the min and max distance lines can be dragged.
class AttenuationGraph : public QWidget {
public:
    std::function<void(float minDistance, float maxDistance)> onRangeEdited;
    std::function<void(float distance)> onDistancePicked;

    explicit AttenuationGraph(QWidget* parent) : QWidget(parent)
    {
        setMinimumHeight(120);
        setMouseTracking(true);
        setToolTip(tr("Volume (dB) over distance as the game hears it. Drag the lines to change where the sound starts "
                      "fading and where it stops; click elsewhere to move the preview source to that distance."));
    }
    void setSound(const engine::SoundDef& s)
    {
        sound_ = s;
        if (drag_ == None)
            span_ = std::max(10.0f, s.maxDistance * 1.2f);
        update();
    }
    void setDistance(float d)
    {
        distance_ = d;
        update();
    }

protected:
    QRectF plot() const { return QRectF(40, 8, width() - 48, height() - 26); }
    static constexpr float kFloorDb = -48.0f; // bottom of the graph
    double yOf(float gain) const
    {
        const float db = std::clamp(20.0f * std::log10(std::max(gain, 1e-6f)), kFloorDb, 0.0f);
        return plot().top() + db / kFloorDb * plot().height();
    }
    double xOf(float d) const { return plot().left() + d / span_ * plot().width(); }
    float dOf(double x) const { return std::max(0.0f, float((x - plot().left()) / plot().width() * span_)); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), kBack);
        const QRectF r = plot();
        p.setPen(kGrid);
        for (int i = 0; i <= 4; ++i) {
            const double y = r.top() + r.height() * i / 4.0;
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
        p.setPen(kText);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        for (int i = 0; i <= 4; ++i)
            p.drawText(QRectF(0, r.top() + r.height() * i / 4.0 - 6, 36, 12), Qt::AlignRight,
                       QString::number(int(kFloorDb * i / 4)) + " dB");
        const float step = span_ > 400 ? 100 : span_ > 150 ? 50 : span_ > 60 ? 20 : span_ > 25 ? 10 : 5;
        for (float d = 0; d <= span_; d += step) {
            p.setPen(kGrid);
            p.drawLine(QPointF(xOf(d), r.top()), QPointF(xOf(d), r.bottom()));
            p.setPen(kText);
            p.drawText(QRectF(xOf(d) - 25, r.bottom() + 2, 50, 14), Qt::AlignCenter, QString::number(d) + " m");
        }
        if (!sound_.spatial) {
            p.setPen(kText);
            p.drawText(r, Qt::AlignCenter, tr("2D sound: same volume everywhere"));
            return;
        }
        QPainterPath curve;
        for (int x = 0; x <= int(r.width()); ++x) {
            const float g = distanceGain(sound_, dOf(r.left() + x));
            const QPointF pt(r.left() + x, yOf(g));
            x == 0 ? curve.moveTo(pt) : curve.lineTo(pt);
        }
        p.setPen(QPen(kAccent, 2));
        p.drawPath(curve);
        auto line = [&](float d, const QString& label, bool hot) {
            p.setPen(QPen(hot ? Qt::white : kWarm, 1, Qt::DashLine));
            p.drawLine(QPointF(xOf(d), r.top()), QPointF(xOf(d), r.bottom()));
            p.setPen(hot ? Qt::white : kWarm);
            p.drawText(QPointF(xOf(d) + 3, r.top() + 11), label);
        };
        line(sound_.minDistance, tr("full"), hover_ == Min || drag_ == Min);
        line(sound_.maxDistance, tr("end"), hover_ == Max || drag_ == Max);
        // the preview source
        const float g = distanceGain(sound_, distance_);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(xOf(std::min(distance_, span_)), yOf(g)), 4, 4);
        p.setPen(Qt::white);
        p.drawText(QRectF(r.right() - 160, r.bottom() - 16, 156, 14), Qt::AlignRight,
                   tr("%1 m: %2 dB").arg(distance_, 0, 'f', 1).arg(20.0 * std::log10(std::max(g, 1e-5f)), 0, 'f', 1));
    }

    enum Handle { None, Min, Max };
    Handle hit(double x) const
    {
        if (!sound_.spatial)
            return None;
        if (std::abs(x - xOf(sound_.minDistance)) < 6)
            return Min;
        if (std::abs(x - xOf(sound_.maxDistance)) < 6)
            return Max;
        return None;
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        drag_ = hit(e->position().x());
        if (drag_ == None && onDistancePicked)
            onDistancePicked(dOf(e->position().x()));
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        const double x = e->position().x();
        if (drag_ == None) {
            const Handle h = hit(x);
            if (h != hover_) {
                hover_ = h;
                setCursor(h == None ? Qt::ArrowCursor : Qt::SizeHorCursor);
                update();
            }
            if ((e->buttons() & Qt::LeftButton) && onDistancePicked)
                onDistancePicked(dOf(x));
            return;
        }
        const float d = std::round(dOf(x) * 10.0f) / 10.0f;
        if (drag_ == Min)
            sound_.minDistance = std::clamp(d, 0.1f, sound_.maxDistance);
        else
            sound_.maxDistance = std::max(d, sound_.minDistance);
        if (onRangeEdited)
            onRangeEdited(sound_.minDistance, sound_.maxDistance);
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (drag_ != None) {
            drag_ = None;
            setSound(sound_); // rescale to the new range
        }
    }

private:
    engine::SoundDef sound_;
    float span_ = 60.0f;
    float distance_ = 3.0f;
    Handle drag_ = None, hover_ = None;
};

// Top-down view around the listener (centre, facing up = -Z). Drag the source.
class SpatialPad : public QWidget {
public:
    std::function<void(glm::vec3)> onMoved;

    explicit SpatialPad(QWidget* parent) : QWidget(parent)
    {
        setMinimumSize(180, 180);
        setToolTip(tr("You are in the middle, looking up. Drag the sound around you while it plays "
                      "(headphones: left / right / behind). Rings: full volume and silence distance."));
    }
    void setSound(const engine::SoundDef& s)
    {
        sound_ = s;
        update();
    }
    // Fits the view to the near field, where direction matters most (wheel zooms out).
    void resetZoom()
    {
        const float far = sound_.spatial ? sound_.maxDistance * 1.1f : 20.0f;
        radius_ = std::clamp(std::max(sound_.minDistance * 5.0f, glm::length(pos_) * 1.5f), 5.0f, std::max(5.0f, far));
        update();
    }
    glm::vec3 position() const { return pos_; }
    void setPosition(glm::vec3 p)
    {
        pos_ = p;
        update();
    }
    QSize sizeHint() const override { return {220, 220}; }

protected:
    float radius() const { return radius_; }
    void wheelEvent(QWheelEvent* e) override
    {
        radius_ = std::clamp(radius_ * (e->angleDelta().y() > 0 ? 0.8f : 1.25f), 2.0f, 5000.0f);
        update();
    }
    QPointF toScreen(glm::vec3 p) const
    {
        const float s = std::min(width(), height()) * 0.5f / radius();
        return {width() * 0.5 + p.x * s, height() * 0.5 + p.z * s};
    }
    glm::vec3 toWorld(QPointF q) const
    {
        const float s = std::min(width(), height()) * 0.5f / radius();
        return {float((q.x() - width() * 0.5) / s), 0.0f, float((q.y() - height() * 0.5) / s)};
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), kBack);
        const QPointF c(width() * 0.5, height() * 0.5);
        const float s = std::min(width(), height()) * 0.5f / radius();
        p.setPen(kGrid);
        p.drawLine(QPointF(c.x(), 0), QPointF(c.x(), height()));
        p.drawLine(QPointF(0, c.y()), QPointF(width(), c.y()));
        if (sound_.spatial) {
            p.setBrush(QColor(86, 156, 214, 40));
            p.setPen(QPen(kAccent, 1));
            p.drawEllipse(c, sound_.minDistance * s, sound_.minDistance * s);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kWarm, 1, Qt::DashLine));
            p.drawEllipse(c, sound_.maxDistance * s, sound_.maxDistance * s);
        }
        // listener: a head with a nose pointing forward (-Z = up)
        p.setPen(Qt::NoPen);
        p.setBrush(kText);
        p.drawEllipse(c, 6, 6);
        p.drawPolygon(QPolygonF({c + QPointF(-4, -4), c + QPointF(4, -4), c + QPointF(0, -12)}));
        const QPointF src = toScreen(pos_);
        p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DotLine));
        p.drawLine(c, src);
        p.setPen(Qt::NoPen);
        p.setBrush(kWarm);
        p.drawEllipse(src, 7, 7);
        p.setPen(kText);
        p.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignBottom,
                   tr("%1 m").arg(glm::length(pos_), 0, 'f', 1));
        p.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom, tr("view %1 m · wheel zooms").arg(radius_, 0, 'f', 0));
    }
    void mousePressEvent(QMouseEvent* e) override { moveTo(e->position()); }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (e->buttons() & Qt::LeftButton)
            moveTo(e->position());
    }

private:
    void moveTo(QPointF q)
    {
        pos_ = toWorld(q);
        update();
        if (onMoved)
            onMoved(pos_);
    }
    engine::SoundDef sound_;
    glm::vec3 pos_{0.0f, 0.0f, -3.0f};
    float radius_ = 20.0f; // metres from the centre to the nearest edge
};

// ------------------------------------------------------------------ editor

SoundEditor::SoundEditor(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QHBoxLayout(this);
    auto* layout = new QVBoxLayout();
    outer->addLayout(layout, 1);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    files_ = new QListWidget(this);
    files_->setMaximumHeight(140);
    files_->setToolTip(tr("Variations: each play picks one at random (never the same twice in a row). Select one to see "
                          "its waveform, double-click to hear it."));
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
    preview->addWidget(play);
    preview->addWidget(stop);
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

    // right side: waveform, attenuation, 3D pad
    auto* side = new QVBoxLayout();
    outer->addLayout(side, 1);
    auto* waveTitle = new QLabel(tr("<b>Waveform</b>"), this);
    waveform_ = new WaveformView(this);
    waveInfo_ = new QLabel(this);
    waveInfo_->setObjectName("dim");
    side->addWidget(waveTitle);
    side->addWidget(waveform_, 2);
    side->addWidget(waveInfo_);
    side->addWidget(new QLabel(tr("<b>Volume over distance</b>"), this));
    attenuation_ = new AttenuationGraph(this);
    side->addWidget(attenuation_, 2);
    side->addWidget(new QLabel(tr("<b>3D preview</b>"), this));
    auto* padRow = new QHBoxLayout();
    pad_ = new SpatialPad(this);
    padRow->addWidget(pad_, 1);
    auto* padSide = new QVBoxLayout();
    orbit_ = new QCheckBox(tr("Orbit around me"), this);
    orbit_->setToolTip(tr("The source circles the listener at its distance (best heard on headphones)"));
    reverb_ = new QComboBox(this);
    for (const std::string& name : engine::reverbPresetNames())
        reverb_->addItem(QString::fromStdString(name));
    reverb_->setToolTip(tr("Room reverb for the preview only (levels set theirs in Environment)"));
    auto* audioInfo = new QLabel(this);
    audioInfo->setObjectName("dim");
    audioInfo->setWordWrap(true);
    {
        engine::AudioSystem& audio = editorAudio();
        audioInfo->setText(audio.hasDevice()
                               ? tr("Output: %1%2").arg(QString::fromStdString(audio.deviceName()), audio.hrtf() ? tr(", HRTF on") : QString())
                               : tr("No audio device: previews are silent."));
    }
    padSide->addWidget(orbit_);
    padSide->addWidget(new QLabel(tr("Preview room"), this));
    padSide->addWidget(reverb_);
    padSide->addWidget(audioInfo);
    padSide->addStretch(1);
    padRow->addLayout(padSide);
    side->addLayout(padRow, 3);

    orbitTimer_ = new QTimer(this);
    orbitTimer_->setInterval(30);
    connect(orbitTimer_, &QTimer::timeout, this, [this] {
        const glm::vec3 p = pad_->position();
        const float r = std::max(glm::length(p), 0.5f);
        const float a = std::atan2(p.z, p.x) + 0.03f * 6.0f / std::max(r, 3.0f) * 3.0f; // ~1 turn in 7 s near you
        pad_->setPosition({r * std::cos(a), 0.0f, r * std::sin(a)});
        updateSourcePosition();
    });
    connect(orbit_, &QCheckBox::toggled, this, [this](bool on) { on ? orbitTimer_->start() : orbitTimer_->stop(); });
    connect(reverb_, &QComboBox::currentTextChanged, this,
            [](const QString& name) { editorAudio().setReverb(name.toStdString(), 1.0f); });
    pad_->onMoved = [this](glm::vec3) { updateSourcePosition(); };
    attenuation_->onDistancePicked = [this](float d) {
        glm::vec3 p = pad_->position();
        const float len = glm::length(p);
        p = len > 0.01f ? p / len * std::max(d, 0.01f) : glm::vec3(0, 0, -std::max(d, 0.01f));
        pad_->setPosition(p);
        updateSourcePosition();
    };
    attenuation_->onRangeEdited = [this](float minD, float maxD) {
        updating_ = true;
        minDistance_->setValue(minD);
        maxDistance_->setValue(maxD);
        updating_ = false;
        edited();
    };

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
    connect(files_, &QListWidget::currentRowChanged, this, &SoundEditor::showWaveform);
    connect(play, &QPushButton::clicked, this, &SoundEditor::play);
    connect(stop, &QPushButton::clicked, this, [this] {
        editorAudio().stop(preview_, 0.15f);
        preview_ = 0;
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
    // start the preview source at a distance where the fade is audible
    pad_->setPosition({0.0f, 0.0f, -std::max(3.0f, sound_.minDistance * 2.0f)});
    showSound();
    pad_->resetZoom();
    if (files_->count() > 0)
        files_->setCurrentRow(0);
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
    const int row = files_->currentRow();
    files_->clear();
    for (const auto& f : sound_.files) {
        auto* item = new QListWidgetItem(QString::fromStdU16String(f.filename().u16string()), files_);
        item->setData(Qt::UserRole, fromPath(f));
        item->setToolTip(fromPath(f));
    }
    if (files_->count() > 0)
        files_->setCurrentRow(std::clamp(row, 0, files_->count() - 1));
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
    attenuation_->setSound(sound_);
    pad_->setSound(sound_);
    attenuation_->setDistance(glm::length(pad_->position()));
    updating_ = false;
    if (files_->count() == 0)
        showWaveform();
}

void SoundEditor::showWaveform()
{
    const int row = files_->currentRow();
    const unsigned request = ++waveRequest_;
    if (row < 0 || row >= static_cast<int>(sound_.files.size())) {
        waveform_->setPeaks({});
        waveInfo_->clear();
        return;
    }
    waveform_->setPeaks({}, tr("Decoding..."));
    const std::filesystem::path file = sound_.files[row];
    static constexpr std::size_t kBuckets = 1200;
    QPointer<SoundEditor> self(this);
    // decode off the UI thread; only the newest request is shown
    engine::JobSystem::global().run([self, file, request] {
        engine::AudioClipData clip;
        std::string error;
        const bool ok = engine::decodeAudioFile(file, clip, &error);
        std::vector<std::vector<std::pair<float, float>>> peaks;
        float peak = 0.0f;
        double sumSquares = 0.0;
        if (ok && clip.channels > 0) {
            const std::size_t frames = clip.frames();
            const int lanes = std::min(clip.channels, 2);
            peaks.assign(lanes, std::vector<std::pair<float, float>>(std::min(kBuckets, std::max<std::size_t>(frames, 1)), {0.0f, 0.0f}));
            const std::size_t buckets = peaks[0].size();
            for (std::size_t i = 0; i < frames; ++i) {
                const std::size_t b = std::min(buckets - 1, i * buckets / std::max<std::size_t>(frames, 1));
                for (int c = 0; c < lanes; ++c) {
                    const float v = clip.samples[i * clip.channels + c] / 32768.0f;
                    auto& [lo, hi] = peaks[c][b];
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                    peak = std::max(peak, std::abs(v));
                    sumSquares += double(v) * v;
                }
            }
            sumSquares /= double(std::max<std::size_t>(frames * lanes, 1));
        }
        QMetaObject::invokeMethod(qApp, [self, request, ok, error, peaks = std::move(peaks), clip = std::move(clip), peak, sumSquares]() mutable {
            if (!self || request != self->waveRequest_)
                return;
            if (!ok) {
                self->waveform_->setPeaks({}, QString::fromStdString(error));
                self->waveInfo_->clear();
                return;
            }
            auto db = [](double linear) { return linear > 1e-6 ? 20.0 * std::log10(linear) : -120.0; };
            const double seconds = clip.sampleRate > 0 ? double(clip.frames()) / clip.sampleRate : 0.0;
            self->waveInfo_->setText(tr("%1 s · %2 Hz · %3 · peak %4 dB · RMS %5 dB")
                                         .arg(seconds, 0, 'f', 2)
                                         .arg(clip.sampleRate)
                                         .arg(clip.channels == 1 ? tr("mono") : clip.channels == 2 ? tr("stereo") : tr("%1 ch").arg(clip.channels))
                                         .arg(db(peak), 0, 'f', 1)
                                         .arg(db(std::sqrt(sumSquares)), 0, 'f', 1)
                                     + (clip.channels > 1 && self->sound_.spatial ? tr(" · mixed to mono in 3D") : QString()));
            self->waveform_->setPeaks(std::move(peaks));
        });
    });
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
    attenuation_->setSound(sound_);
    pad_->setSound(sound_);
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
}

void SoundEditor::updateSourcePosition()
{
    const glm::vec3 p = pad_->position();
    attenuation_->setDistance(glm::length(p));
    if (preview_ && sound_.spatial)
        editorAudio().setPosition(preview_, p);
}

void SoundEditor::play()
{
    engine::AudioSystem& audio = editorAudio();
    audio.stop(preview_);
    audio.setListener({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    // The edited (maybe unsaved) settings play as they are, from the pad's position.
    preview_ = sound_.spatial ? audio.play(sound_, pad_->position()) : audio.play2D(sound_);
}
