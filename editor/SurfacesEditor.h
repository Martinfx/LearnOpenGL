#pragma once
#include <engine/Surfaces.h>

#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QTableWidget;

// Editor tab for assets/surfaces.cfg: per surface type (concrete, metal, wood...)
// the footstep, impact sound and particles, case landing sound and bullet hole. Materials pick
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
    void markEdited() { edited(); }

signals:
    void modificationChanged();
    void openAsset(const QString& path); // "Edit" next to an impact effect

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
    std::vector<engine::SurfaceType> originals_; // rows as loaded, for fields without a column
};
