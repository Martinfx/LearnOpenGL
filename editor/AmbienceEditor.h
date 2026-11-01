#pragma once
#include <engine/Ambience.h>
#include <engine/Audio.h>

#include <QWidget>

class QTableWidget;
class QPushButton;

// Editor tab for an .ambience set (engine/Ambience.h): looping layers with their
// volume by day and by night, and random sounds played now and then somewhere
// around the listener (interval, distance, chance by day / night, height, how
// much the ambience ducks for them). Preview plays the layers in the editor.
class AmbienceEditor : public QWidget {
    Q_OBJECT
public:
    explicit AmbienceEditor(QWidget* parent = nullptr);
    ~AmbienceEditor() override;
    bool load(const QString& path, QString* error);
    bool save(QString* error);
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();

private:
    void rebuild();
    void edited();
    void stopPreview();
    void togglePreview();
    void playRandom();

    QString path_;
    engine::AmbienceSet set_;
    bool modified_ = false;
    bool updating_ = false;
    QTableWidget* layers_ = nullptr;
    QTableWidget* randoms_ = nullptr;
    QPushButton* preview_ = nullptr;
    std::vector<engine::AudioSystem::Handle> playing_;
};
