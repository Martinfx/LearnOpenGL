#pragma once
#include <engine/Scene.h>

#include <QMainWindow>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QWidget;
class Vec3Editor;
class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void buildDocks();

    void newLevel();
    void openLevel();
    bool save();
    bool saveAs();
    bool saveTo(const QString& path);
    bool confirmDiscard();
    void playLevel();

    void addBox();
    void importModel();
    void addEntity(const QString& type);
    void browseScript();
    void duplicateBox();
    void deleteBox();
    void select(int index);

    void refreshOutliner();
    void refreshProperties();
    void refreshLevelProperties();
    void markDirty();
    void updateTitle();
    engine::Box* selectedBox();
    engine::Model* selectedModel();
    engine::Entity* selectedEntity();

    engine::Scene scene_;
    int selected_ = -1;
    QString currentFile_;
    bool dirty_ = false;

    Viewport* viewport_ = nullptr;
    QListWidget* outliner_ = nullptr;
    QWidget* boxPanel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* kindLabel_ = nullptr;
    QLineEdit* typeEdit_ = nullptr;
    QDoubleSpinBox* yawSpin_ = nullptr;
    QLineEdit* scriptEdit_ = nullptr;
    QWidget* scriptRow_ = nullptr;
    Vec3Editor* positionEdit_ = nullptr;
    Vec3Editor* sizeEdit_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    Vec3Editor* playerStartEdit_ = nullptr;
    Vec3Editor* sunEdit_ = nullptr;
};
