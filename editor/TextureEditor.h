#pragma once
#include <engine/Material.h>

#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScrollArea;

// Sampling and color adjustments of one image (wrap, filtering, mipmaps, sRGB,
// brightness/contrast/saturation/tint, flip). Saved next to the image as
// "<image>.texture"; the image file is never modified. Changes are live.
class TextureEditor : public QWidget {
    Q_OBJECT
public:
    explicit TextureEditor(QWidget* parent = nullptr);

    bool load(const QString& image, QString* error = nullptr);
    bool save(QString* error = nullptr);
    bool isModified() const { return modified_; }
    QString path() const;
    QString title() const;

signals:
    void textureChanged();
    void modificationChanged(bool modified);

private:
    void refreshForm();
    void refreshPreview();
    void edited();
    void revert();

    std::shared_ptr<engine::TextureAsset> asset_;
    bool modified_ = false;
    bool loading_ = false;
    QLabel* image_;
    QScrollArea* scroll_;
    QComboBox* zoom_;
    QCheckBox* tile_;
    QLabel* info_;
    QWidget* form_;
    QComboBox* wrap_;
    QComboBox* filter_;
    QCheckBox* mipmaps_;
    QCheckBox* srgb_;
    QCheckBox* flipY_;
    QCheckBox* compress_;
    QComboBox* maxSize_;
    QDoubleSpinBox* brightness_;
    QDoubleSpinBox* contrast_;
    QDoubleSpinBox* saturation_;
    QPushButton* tint_;
};
