#pragma once
#include <engine/ScriptDebug.h>
#include <map>
#include <set>
#include <engine/Factions.h>
#include <array>
#include <atomic>
#include <memory>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/FileWatcher.h>

#include "EditorHistory.h"

#include <QMainWindow>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

class QComboBox;
class QSpinBox;
class QTabWidget;
class ScriptEditor;
class MaterialEditor;
class TextureEditor;
class SoundPicker;
class SoundEditor;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QTableWidget;
class QLineEdit;
class QListWidget;
class HierarchyTree;
class QUndoStack;
class AssetBrowser;
class DependencyPanel;
class QPushButton;
class QWidget;
class Vec3Editor;
class Viewport;
class WeaponPanel;
class EnvironmentPanel;
class ViewportPanel;
class OutlinerPanel;
class CollapsibleSection;
class QFormLayout;
class QTabBar;
class QPlainTextEdit;
class QProcess;
class QDockWidget;
class QStackedWidget;
class QMenu;
class QAction;
class QFormLayout;
struct TerrainUi;
namespace engine { struct Terrain; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    bool openFile(const QString& path);   // loads in the background
    void openAssetFile(const QString& path); // in its editor (sound, particle effect, material...)
    // After a crash: offers to reopen the level autosaved before it (call once the window is up).
    void offerRecovery();
signals:
    void levelOpened();
    void loadingProgress(int percent, const QString& step); // while a level loads
    void loadingFinished();
public:

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void buildDocks();
    void buildStatusBar();
    void buildModePanel();          // left panel: Select / Place / Model modes
    void setEditorMode(int mode);   // 0 select, 1 place, 2 model, 3 landscape, 4 foliage, 5 mesh paint
    // Landscape / Foliage modes (TerrainTools.cpp).
    void buildTerrainPages();
    void refreshTerrainPanel();
    void updateTerrainBrush();
    void addFoliageType(const QString& path);
    void loadFoliageModel(const std::filesystem::path& file);
    engine::Terrain* editTerrain(); // copy on write, for undo
    void terrainStroke(glm::vec3 point, Qt::KeyboardModifiers modifiers, bool first);
    void terrainStrokeFinished();
    TerrainUi* terrainUi_ = nullptr;
    // Mesh paint mode (MeshPaintTools.cpp): vertex colours of the selected model blend in its materials' layers.
    void buildMeshPaintPage();
    void refreshMeshPaintPanel();
    void meshPaintStroke(glm::vec3 point, Qt::KeyboardModifiers modifiers, bool first);
    void meshPaintFinished();
    void updateMeshPaintBrush();
    struct MeshPaintUi* meshPaintUi_ = nullptr;

public:
    void notify(const QString& message, int timeoutMs = 0); // status line + output log
    void appendLog(const QString& line, int level);         // 0 info, 1 warning, 2 error
    void runCommand(const QString& command);                // console (Cmd field)
private:
    void showLoading(const QString& name);
    void updateLoading(float fraction, const QString& step);
    void hideLoading();
    void pauseGame();
    void stopGame();
    void shutdownGame(); // editor closing: end the game child without signals into a dying window
    void updatePlayButtons();
    void saveAll();
    void updateDetailsVisibility();
    void refreshTransformBox();
    void applyTransformBox(int field, glm::vec3 value, bool delta);
    void refreshTexturePanel();
    void editBoxUv(const std::function<void(engine::Box&)>& edit);
    void refreshMaterialList();

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
    void openShaderEditor();
    void applyShaders();
    void setRenderQuality(const engine::RenderQuality& quality);
    bool closeScriptTab(int index);
    bool saveAllScripts();
    bool confirmCloseScripts();
    void saveCurrent();
    MaterialEditor* openMaterialEditor(const QString& material = QString());
    TextureEditor* openTexture(const QString& image);
    SoundEditor* openSoundEditor(const QString& path);
    void openSurfacesEditor();
    void openFactionsEditor();
    void openStoryEditor(const QString& path); // .dialog / .quest
    void newStoryFile(bool dialog);            // Assets > New dialog / New quest
    void reloadFactionList(); // Properties > Character > Faction offers the factions of assets/factions.cfg
    void openParticleEditor(const QString& path);
    void openAnimGraphEditor(const QString& path);
    void newAnimGraph(const QString& folder);
    void newParticleEffect(const QString& folder);
    void newSound(const QString& folder);
    void newAmbience(const QString& folder);
    void openAmbienceEditor(const QString& path);
    void browseBoxMaterial();
    void refreshMaterialSlots();

    void addBox();
    void importModel();
    // Loads in the background, bottom of the model on `point`; `parent` = group to put it in.
    bool placeModel(const QString& path, glm::vec3 point, const std::string& parent = {});
    // Prefabs (engine/Prefab.h): place an instance, the outliner's group actions.
    void placePrefab(const QString& path, glm::vec3 point);
    void prefabAction(const QString& group, const QString& action);
    int selectedPrefab() const; // instance of the selected object, -1
    std::filesystem::path levelDir() const;
    void applyLoadedLevel(const QString& path, engine::Scene loaded);
    void dropToFloor();
    void insertModel(const QString& path, engine::Model model, glm::vec3 point, const std::string& parent = {});
    bool assignMaterial(const QString& material, int object);
    void openAsset(const QString& path, int type);
    // Content Browser: rename / move an asset with the references to it (engine/AssetRegistry.h).
    void renameAsset(const QString& from, const QString& to);
    void showAssetReferences(const QString& path);
    void addEntity(const QString& type);
    void bakeProbes(); // Render > Bake lighting probes: <level>.probes
    void applyProbeIntensity(const std::string& name, float intensity); // live, without a new bake
    void addAnomaly(int kind); // engine::AnomalyKind
    void addLight(bool spot);
    void addCampfire(); // group: fire pit model, flames, flickering light, crackle, sit points
    void addDogPack();  // group: a lair (A-life point) and four feral dogs sharing a Group
    void applyPropCatalogueToLevel(); // every model the catalogue lists
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
    void resetHistory();                 // after a level was opened or created
    void applySnapshot(const SceneSnapshot& snapshot);
    void updateTitle();
    engine::Box* selectedBox();
    engine::Model* selectedModel();
    engine::Entity* selectedEntity();

    engine::Scene scene_;
    int selected_ = -1;
    QString currentFile_;
    bool dirty_ = false;

    Viewport* viewport_ = nullptr;
    ViewportPanel* viewportPanel_ = nullptr;
    OutlinerPanel* outlinerPanel_ = nullptr;
    QMenu* addMenu_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* playAction_ = nullptr;
    std::vector<QAction*> modeActions_;
    QProcess* game_ = nullptr;
    bool gamePaused_ = false;
    // Script debugger: breakpoints by file key ("scripts/enemy.lua"), the pause being read from
    // the game's output and the panel showing it.
    std::map<std::string, std::set<int>> breakpoints_;
    engine::ScriptDebugPause pendingPause_;
    class ScriptDebugPanel* debugPanel_ = nullptr;
    QDockWidget* debugDock_ = nullptr;
    QString executionScript_; // absolute path of the script showing the paused line
    void sendDebug(const QString& line);
    void debugCommand(const QString& word);
    void onScriptPaused();
    void onScriptResumed();
    void showScriptLine(const QString& key, int line, bool execution);
    void attachBreakpoints(class ScriptEditor* se);
    QPlainTextEdit* log_ = nullptr;
    QDockWidget* logDock_ = nullptr;
    QDockWidget* assetsDock_ = nullptr;
    QDockWidget* modesDock_ = nullptr;
    QStackedWidget* modePages_ = nullptr;
    QLabel* statusMessage_ = nullptr;
    QLabel* detailsIcon_ = nullptr;
    QLineEdit* detailsSearch_ = nullptr;
    QTabBar* detailsTabs_ = nullptr;
    struct DetailSection {
        CollapsibleSection* section;
        QFormLayout* form;
        QString category;
    };
    std::vector<DetailSection> detailSections_;
    QWidget* texturePanel_ = nullptr;
    QWidget* lodRow_ = nullptr;
    class QFrame* loadingPanel_ = nullptr;
    QLabel* loadingTitle_ = nullptr;
    QLabel* loadingStep_ = nullptr;
    class QProgressBar* loadingBar_ = nullptr;
    class QProgressBar* statusBar_ = nullptr;
    QLineEdit* loadingImageEdit_ = nullptr;
    QLineEdit* mapImageEdit_ = nullptr;
    Vec3Editor* mapBoundsEdit_ = nullptr;
    QLineEdit* loadingTitleEdit_ = nullptr;
    QLineEdit* loadingHintEdit_ = nullptr;
    QLineEdit* lodEdit_ = nullptr;
    QLineEdit* farLodEdit_ = nullptr;
    QDoubleSpinBox* lodDistanceSpin_ = nullptr;
    QDoubleSpinBox* cullDistanceSpin_ = nullptr;
    QCheckBox* collisionCheck_ = nullptr;
    QCheckBox* dynamicCheck_ = nullptr;          // model: physics prop
    class OutputsPanel* outputsPanel_ = nullptr;  // entity I/O connections
    QFormLayout* outputsForm_ = nullptr;
    QFormLayout* reverbForm_ = nullptr;           // entity type "reverb"
    QFormLayout* interiorForm_ = nullptr;         // entity type "interior"
    QFormLayout* reflectionForm_ = nullptr;       // entity type "reflection_probe"
    QFormLayout* irradianceForm_ = nullptr;       // entity type "irradiance_volume"
    QDoubleSpinBox* reflFade_ = nullptr;
    QDoubleSpinBox* reflIntensity_ = nullptr;
    QCheckBox* reflParallax_ = nullptr;
    QComboBox* reflResolution_ = nullptr;
    QDoubleSpinBox* irrSpacing_ = nullptr;
    QDoubleSpinBox* irrFade_ = nullptr;
    QDoubleSpinBox* irrIntensity_ = nullptr;
    QLabel* irrPoints_ = nullptr;
    QDoubleSpinBox* interiorSky_ = nullptr;
    QDoubleSpinBox* interiorFog_ = nullptr;
    QDoubleSpinBox* interiorFade_ = nullptr;
    QComboBox* interiorPreset_ = nullptr;
    QDoubleSpinBox* interiorAmount_ = nullptr;
    QFormLayout* waterForm_ = nullptr;            // entity type "water"
    QFormLayout* prefabForm_ = nullptr;           // object of a prefab instance
    QLabel* prefabLabel_ = nullptr;
    class Vec3Editor* prefabPosition_ = nullptr;
    QDoubleSpinBox* prefabYaw_ = nullptr;
    QDoubleSpinBox* waterClarity_ = nullptr;
    QDoubleSpinBox* waterFlow_ = nullptr;
    QDoubleSpinBox* waterWaves_ = nullptr;
    QDoubleSpinBox* waterFoam_ = nullptr;
    QDoubleSpinBox* waterReflection_ = nullptr;
    QFormLayout* pickupForm_ = nullptr;           // entity type "pickup"
    QComboBox* pickupItem_ = nullptr;
    QLineEdit* pickupAmmoType_ = nullptr;
    QSpinBox* pickupAmount_ = nullptr;
    QLineEdit* pickupWeapon_ = nullptr;
    QLineEdit* pickupName_ = nullptr;
    void addPickup(const QString& item, const QString& model);
    QFormLayout* ambienceForm_ = nullptr;         // entity type "ambience"
    SoundPicker* ambienceSet_ = nullptr;
    QDoubleSpinBox* ambienceFade_ = nullptr;
    QDoubleSpinBox* ambienceOutside_ = nullptr;
    QComboBox* reverbPreset_ = nullptr;
    QDoubleSpinBox* reverbAmount_ = nullptr;
    QDoubleSpinBox* reverbFade_ = nullptr;
    bool pickingTarget_ = false;                  // next viewport click picks an I/O target
    void addLogicEntity(const QString& type);     // "trigger" volume or "relay"
    QDoubleSpinBox* massSpin_ = nullptr;
    QComboBox* colliderCombo_ = nullptr;
    // Physics of a prop from assets/models/props/clutter/clutter.json (dynamic, mass, collider);
    // false when the catalogue does not list the model.
    bool applyPropCatalogue(engine::Model& model);
    QCheckBox* shadowsCheck_ = nullptr;
    QDoubleSpinBox* swaySpin_ = nullptr;      // Model::sway
    QDoubleSpinBox* impostorSpin_ = nullptr;  // Model::impostorDistance
    QDoubleSpinBox* uvSpins_[5] = {};   // scale U, V, shift U, V, rotation
    QComboBox* activeMaterial_ = nullptr;
    QLabel* selectionInfo_ = nullptr;   // Select mode page
    QStringList commandHistory_;
    QTabWidget* tabs_ = nullptr;
    MaterialEditor* materialEditor_ = nullptr;
    class ShaderEditor* shaderEditor_ = nullptr;
    bool customShaders_ = false; // the viewport runs shaders from shaderRoot()
    QWidget* materialRow_ = nullptr;
    QLineEdit* materialEdit_ = nullptr;
    QTableWidget* materialSlots_ = nullptr;
    WeaponPanel* weapons_ = nullptr;
    EnvironmentPanel* environment_ = nullptr;
    HierarchyTree* outliner_ = nullptr;
    DependencyPanel* dependencies_ = nullptr;
    AssetBrowser* assets_ = nullptr;
    QUndoStack* history_ = nullptr;
    MeshStore meshStore_;
    std::shared_ptr<SceneSnapshot> lastState_;
    bool restoring_ = false;
    QWidget* boxPanel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* kindLabel_ = nullptr;
    QLineEdit* typeEdit_ = nullptr;
    QDoubleSpinBox* yawSpin_ = nullptr;
    QLineEdit* scriptEdit_ = nullptr;
    QWidget* scriptRow_ = nullptr;
    QTableWidget* scriptParams_ = nullptr;
    QWidget* characterRow_ = nullptr;
    SoundPicker* entitySound_ = nullptr;
    QFormLayout* effectForm_ = nullptr;          // entity type "particle"
    QFormLayout* anomalyForm_ = nullptr;         // entity type "anomaly"
    class AnomalyPanel* anomalyPanel_ = nullptr;
    QFormLayout* lightForm_ = nullptr;           // entity type "light"
    class LightPanel* lightPanel_ = nullptr;
    QLineEdit* entityEffect_ = nullptr;          // its .particle (relative to the assets)      // entity type "sound": what plays
    QCheckBox* soundZoneCheck_ = nullptr;     // ambient zone filling the entity box
    QDoubleSpinBox* soundFadeSpin_ = nullptr; // zone fade-out distance
    QDoubleSpinBox* soundVolumeSpin_ = nullptr;
    std::array<SoundPicker*, 3> playerSounds_{}; // footsteps, landing, hurt
    QLineEdit* characterEdit_ = nullptr;
    QDoubleSpinBox* modelScaleSpin_ = nullptr;
    QDoubleSpinBox* modelYawSpin_ = nullptr;
    QComboBox* animationCombo_ = nullptr;
    QLineEdit* animGraphEdit_ = nullptr;
    QWidget* animGraphRow_ = nullptr;
    std::vector<engine::ScriptProperty> scriptProps_;
    std::vector<std::pair<QDoubleSpinBox*, float engine::PlayerSettings::*>> playerSpins_;
    QCheckBox* flashlightCheck_ = nullptr;
    QComboBox* characterFlashlight_ = nullptr;  // entity property "flashlight": none / "1" / "night"
    QComboBox* characterFaction_ = nullptr;     // entity property "faction" (empty = by type)
    QLineEdit* characterGroup_ = nullptr;       // entity property "group": squad that calls each other for help
    QLineEdit* characterDialog_ = nullptr;      // entity property "dialog": .dialog the player talks through (E)
    engine::FactionTable factionList_;
    QCheckBox* flashlightShadowsCheck_ = nullptr;
    QPushButton* flashlightColor_ = nullptr;
    // Navigation (Level details): settings, build on the job system, viewport overlay.
    void buildNavMesh();
    void setNavOutOfDate();
    std::vector<std::pair<QDoubleSpinBox*, float engine::NavSettings::*>> navSpins_;
    class QCheckBox* navEnabled_ = nullptr;
    class QLabel* navStatus_ = nullptr;
    class QPushButton* navBuild_ = nullptr;
    class QTimer* navPoll_ = nullptr;
    class QTimer* navAutoBuild_ = nullptr;
    QAction* navShowAction_ = nullptr;
    struct NavJob {
        std::atomic<float> progress{0.0f};
        std::atomic<bool> cancel{false};
        std::atomic<bool> done{false};
    };
    std::shared_ptr<NavJob> navJob_;
    std::shared_ptr<engine::NavMesh> navMesh_; // last finished build
    engine::DebugGeometry navDebug_;
    bool navStale_ = true;
    // World streaming (Level details): settings and a cell overlay in the viewport.
    void refreshStreamingOverlay();
    class QCheckBox* streamEnabled_ = nullptr;
    class QCheckBox* streamKeepLod_ = nullptr;
    class QCheckBox* streamShow_ = nullptr;
    class QSpinBox* streamBudget_ = nullptr;
    class QLabel* streamStatus_ = nullptr;
    class QTimer* streamTimer_ = nullptr;
    std::vector<std::pair<QDoubleSpinBox*, float engine::StreamingSettings::*>> streamSpins_;
    engine::DebugGeometry streamDebug_;
    // Hot reload of assets changed on disk (Blender export, image editor): engine/HotReload.h.
    std::unique_ptr<engine::FileWatcher> watcher_;
    // Memory dock: budget by category (Scene::memoryBudget).
    class QTableWidget* budgetTable_ = nullptr;
    std::vector<QDoubleSpinBox*> budgetSpins_;
    int logListener_ = 0; // engine log -> Output Log
    // Crash recovery: the edited level is saved to the user folder every 90 s while unsaved.
    void autosave();
    void clearAutosave();
    bool autosaving_ = false;
    Vec3Editor* positionEdit_ = nullptr;
    Vec3Editor* sizeEdit_ = nullptr;
    Vec3Editor* rotationEdit_ = nullptr;   // glTF models
    QDoubleSpinBox* scaleSpin_ = nullptr;  // glTF models
    QPushButton* colorButton_ = nullptr;
    Vec3Editor* playerStartEdit_ = nullptr;
    Vec3Editor* sunEdit_ = nullptr;
};
