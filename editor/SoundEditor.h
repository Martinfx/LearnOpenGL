#pragma once
#include <engine/Audio.h>

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTimer;
class WaveformView;
class AttenuationGraph;
class SpatialPad;

// The editor's audio output, for previews (opened on first use, never blocks:
// files decode on miniaudio's job thread).
engine::AudioSystem& editorAudio();
// Plays a .sound or audio file once, without 3D (the editor's preview).
void previewSound(const QString& path);
void stopSoundPreview();

// A sound slot in a form: the path (shown relative to the assets), browse,
// preview and clear. Emits pathChanged with the absolute path ("" = none).
class SoundPicker : public QWidget {
    Q_OBJECT
public:
    explicit SoundPicker(QWidget* parent = nullptr);
    void setPath(const QString& absolutePath); // no signal
    QString path() const { return path_; }

signals:
    void pathChanged(const QString& absolutePath);

private:
    void updateText();
    QString path_;
    QLineEdit* edit_ = nullptr;
};

// Editor tab for a .sound file: variations, volume, random pitch, 3D range,
// loop / stream, mixer group. Preview plays one variation like the game would.
// The right side shows the selected variation's waveform (decoded on the job
// system), the distance attenuation curve (drag its min / max lines) and a
// top-down 3D pad: drag the sound around the listener while it plays, or let
// it orbit, with a reverb preset to hear it in a room.
class SoundEditor : public QWidget {
    Q_OBJECT
public:
    explicit SoundEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error);
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();

private:
    void showSound();
    void edited();
    void play();
    void showWaveform();
    void updateSourcePosition();

    QString path_;
    engine::SoundDef sound_;
    bool modified_ = false;
    bool updating_ = false;
    engine::AudioSystem::Handle preview_ = 0;

    QListWidget* files_ = nullptr;
    QDoubleSpinBox* volume_ = nullptr;
    QDoubleSpinBox* pitchMin_ = nullptr;
    QDoubleSpinBox* pitchMax_ = nullptr;
    QCheckBox* spatial_ = nullptr;
    QDoubleSpinBox* minDistance_ = nullptr;
    QDoubleSpinBox* maxDistance_ = nullptr;
    QDoubleSpinBox* rolloff_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QCheckBox* stream_ = nullptr;
    QComboBox* group_ = nullptr;
    QSpinBox* maxInstances_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* waveInfo_ = nullptr;
    WaveformView* waveform_ = nullptr;
    AttenuationGraph* attenuation_ = nullptr;
    SpatialPad* pad_ = nullptr;
    QCheckBox* orbit_ = nullptr;
    QComboBox* reverb_ = nullptr;
    QTimer* orbitTimer_ = nullptr;
    unsigned waveRequest_ = 0; // newest decode wins
};
