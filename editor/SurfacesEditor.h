#pragma once
#include <engine/Surfaces.h>

#include <QWidget>

class QComboBox;
class QLabel;
class QTableWidget;

// Editor tab for assets/surfaces.cfg: per surface type (concrete, metal, wood...)
// the footstep and bullet impact sounds and the bullet hole size. Materials pick
// their surface in the Material editor (or it is guessed from their name).
class SurfacesEditor : public QWidget {
    Q_OBJECT
public:
    explicit SurfacesEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error);
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();

private:
    void addRow(const engine::SurfaceType& type);
    void refreshDefault();
    void edited();
    engine::SurfaceTable collect() const;

    QString path_;
    bool modified_ = false;
    bool updating_ = false;
    QTableWidget* table_ = nullptr;
    QComboBox* fallback_ = nullptr;
};
