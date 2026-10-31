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
};
