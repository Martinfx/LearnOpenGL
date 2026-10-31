#pragma once
#include <engine/Renderer.h>
#include <engine/Scene.h>

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Spinning sphere and cube drawn with the engine renderer, for material previews.
class MaterialPreview : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MaterialPreview(QWidget* parent = nullptr);
    ~MaterialPreview() override;
    void setMaterial(std::shared_ptr<engine::Material> material);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void cleanup();

    engine::Renderer renderer_;
    engine::Scene scene_;
    bool ready_ = false;
    QTimer timer_;
    QElapsedTimer clock_;
};

// Edits .material files: color, texture, tiling, highlights, glow, world mapping.
// Changes are live in every view that uses the material; Save writes the file.
class MaterialEditor : public QWidget {
    Q_OBJECT
public:
    explicit MaterialEditor(QWidget* parent = nullptr);

    void refreshList();
    void open(const QString& path);
    bool saveModified(QString* error = nullptr); // saves every material changed here
    bool hasUnsaved() const { return !modified_.isEmpty(); }
    QString currentPath() const;

signals:
    void materialChanged();                 // any value changed (repaint the level)
    void editTexture(const QString& image); // open the texture editor
    void createdMaterial(const QString& path);

private:
    void select(const QString& path);
    void refreshForm();
    void edited();
    void newMaterial();
    void duplicateMaterial();
    bool saveCurrent();
    void revertCurrent();

    std::shared_ptr<engine::Material> material_;
    QSet<QString> modified_;
    QListWidget* list_;
    QWidget* form_;
    QLineEdit* nameEdit_;
    QPushButton* colorButton_;
    QLineEdit* mapEdits_[engine::kMapCount] = {};
    QPushButton* mapEditButtons_[engine::kMapCount] = {};
    QDoubleSpinBox* tilingX_;
    QDoubleSpinBox* tilingY_;
    QDoubleSpinBox* roughness_;
    QDoubleSpinBox* metallic_;
    QDoubleSpinBox* normalStrength_;
    QPushButton* emissiveButton_;
    QCheckBox* worldUv_;
    QLabel* pathLabel_;
    MaterialPreview* preview_;
    bool loading_ = false;
};
