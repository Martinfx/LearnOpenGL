#pragma once
#include <engine/Factions.h>

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

// Editor tab for assets/factions.cfg: the factions (id, name, colour), the player's faction,
// the hostile / friendly thresholds and the relation of every pair as a matrix (symmetric;
// red = hostile, grey = neutral, green = friendly). NPCs pick their faction in
// Properties > Character.
class FactionsEditor : public QWidget {
    Q_OBJECT
public:
    explicit FactionsEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error);
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();
    void saved(); // the file changed: the Properties faction list reloads it

private:
    void refresh();         // all widgets from table_
    void refreshMatrix();
    void edited();
    void renameFaction(const std::string& from, const std::string& to);

    QString path_;
    engine::FactionTable table_;
    bool modified_ = false;
    bool updating_ = false;
    QComboBox* player_ = nullptr;
    QDoubleSpinBox* hostile_ = nullptr;
    QDoubleSpinBox* friendly_ = nullptr;
    QTableWidget* factions_ = nullptr;
    QTableWidget* matrix_ = nullptr;
};
