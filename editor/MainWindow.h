#pragma once
#include <engine/Scene.h>

#include <QMainWindow>
#include <utility>
#include <vector>

class QComboBox;
class QTabWidget;
class ScriptEditor;
class MaterialEditor;
class TextureEditor;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;
class QLineEdit;
class QListWidget;
class HierarchyTree;
class AssetBrowser;
class DependencyPanel;
class QPushButton;
class QWidget;
class Vec3Editor;
class Viewport;
class WeaponPanel;

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

    // Script editor tabs (next to the 3D view).
    ScriptEditor* currentScript() const;
    ScriptEditor* openScript(const QString& path);
    void newScript();
    void openScriptDialog();
    bool closeScriptTab(int index);
    bool saveAllScripts();
    bool confirmCloseScripts();
    void saveCurrent();
    MaterialEditor* openMaterialEditor(const QString& material = QString());
    TextureEditor* openTexture(const QString& image);
    void browseBoxMaterial();
    void refreshMaterialSlots();

    void addBox();
    void importModel();
    bool placeModel(const QString& path, glm::vec3 point); // bottom of the model on `point`
    bool assignMaterial(const QString& material, int object);
    void openAsset(const QString& path, int type);
    void addEntity(const QString& type);
    void browseScript();
    void browseCharacter();
    void refreshCharacter();
    void duplicateBox();
    void deleteBox();
    void select(int index);

    void refreshOutliner();
    void refreshProperties();
    void refreshLevelProperties();
    void refreshScriptParams();
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
    QTabWidget* tabs_ = nullptr;
    MaterialEditor* materialEditor_ = nullptr;
    QWidget* materialRow_ = nullptr;
    QLineEdit* materialEdit_ = nullptr;
    QTableWidget* materialSlots_ = nullptr;
    WeaponPanel* weapons_ = nullptr;
    HierarchyTree* outliner_ = nullptr;
    DependencyPanel* dependencies_ = nullptr;
    AssetBrowser* assets_ = nullptr;
    QWidget* boxPanel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* kindLabel_ = nullptr;
    QLineEdit* typeEdit_ = nullptr;
    QDoubleSpinBox* yawSpin_ = nullptr;
    QLineEdit* scriptEdit_ = nullptr;
    QWidget* scriptRow_ = nullptr;
    QTableWidget* scriptParams_ = nullptr;
    QWidget* characterRow_ = nullptr;
    QLineEdit* characterEdit_ = nullptr;
    QDoubleSpinBox* modelScaleSpin_ = nullptr;
    QDoubleSpinBox* modelYawSpin_ = nullptr;
    QComboBox* animationCombo_ = nullptr;
    std::vector<engine::ScriptProperty> scriptProps_;
    std::vector<std::pair<QDoubleSpinBox*, float engine::PlayerSettings::*>> playerSpins_;
    Vec3Editor* positionEdit_ = nullptr;
    Vec3Editor* sizeEdit_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    Vec3Editor* playerStartEdit_ = nullptr;
    Vec3Editor* sunEdit_ = nullptr;
};
