#include "TextureEditor.h"

#include <engine/Scene.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
QString colorStyle(glm::vec3 c)
{
    return QString("background-color: %1; min-height: 22px;").arg(QColor::fromRgbF(c.r, c.g, c.b).name());
}
} // namespace

TextureEditor::TextureEditor(QWidget* parent)
    : QWidget(parent)
{
    // Left: preview.
    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto* viewRow = new QHBoxLayout();
    zoom_ = new QComboBox(left);
    for (int z : {25, 50, 100, 200, 400})
        zoom_->addItem(QString("%1 %").arg(z), z);
    zoom_->setCurrentIndex(1);
    tile_ = new QCheckBox(tr("Show tiled 3 x 3 (check seams)"), left);
    viewRow->addWidget(new QLabel(tr("Zoom"), left));
    viewRow->addWidget(zoom_);
    viewRow->addWidget(tile_);
    viewRow->addStretch(1);
    image_ = new QLabel(left);
    image_->setAlignment(Qt::AlignCenter);
    scroll_ = new QScrollArea(left);
    scroll_->setWidget(image_);
    scroll_->setWidgetResizable(true);
    scroll_->setStyleSheet("QScrollArea { background: #3a3a3a; }");
    info_ = new QLabel(left);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info_->setWordWrap(true);
    leftLayout->addLayout(viewRow);
    leftLayout->addWidget(scroll_, 1);
    leftLayout->addWidget(info_);

    // Right: settings.
    form_ = new QWidget(this);
    auto* form = new QFormLayout(form_);
    wrap_ = new QComboBox(form_);
    wrap_->addItems({tr("Repeat"), tr("Clamp to edge"), tr("Mirrored repeat")});
    filter_ = new QComboBox(form_);
    filter_->addItems({tr("Smooth (linear)"), tr("Pixelated (nearest)")});
    mipmaps_ = new QCheckBox(tr("Mipmaps (no shimmering in the distance)"), form_);
    srgb_ = new QCheckBox(tr("Color texture (sRGB)"), form_);
    srgb_->setToolTip(tr("Off for data textures such as masks or roughness"));
    flipY_ = new QCheckBox(tr("Flip vertically"), form_);
    compress_ = new QCheckBox(tr("GPU compression (BC1/BC3, BC5 for normal maps)"), form_);
    compress_->setToolTip(tr("Takes 1/4 to 1/8 of the memory of plain RGBA. Off for pixel art or UI images "
                             "that must stay exact (nearest filtering is never compressed)."));
    maxSize_ = new QComboBox(form_);
    maxSize_->addItem(tr("Original"), 0);
    for (int size : {4096, 2048, 1024, 512, 256, 128})
        maxSize_->addItem(QString("%1 px").arg(size), size);
    maxSize_->setToolTip(tr("Longest side in the game; larger images are scaled down when cooked"));
    auto makeSpin = [this](double min, double max, double step) {
        auto* s = new QDoubleSpinBox(form_);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(2);
        connect(s, &QDoubleSpinBox::valueChanged, this, &TextureEditor::edited);
        return s;
    };
    brightness_ = makeSpin(-1.0, 1.0, 0.05);
    contrast_ = makeSpin(0.0, 3.0, 0.05);
    saturation_ = makeSpin(0.0, 3.0, 0.05);
    tint_ = new QPushButton(form_);
    auto* save = new QPushButton(tr("Save"), form_);
    auto* revert = new QPushButton(tr("Revert"), form_);
    auto* reset = new QPushButton(tr("Reset adjustments"), form_);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(save);
    buttons->addWidget(revert);
    form->addRow(new QLabel(tr("<b>Sampling</b>"), form_));
    form->addRow(tr("Wrap"), wrap_);
    form->addRow(tr("Filter"), filter_);
    form->addRow(QString(), mipmaps_);
    form->addRow(QString(), srgb_);
    form->addRow(new QLabel(tr("<b>Memory</b>"), form_));
    form->addRow(QString(), compress_);
    form->addRow(tr("Max size"), maxSize_);
    form->addRow(new QLabel(tr("<b>Adjustments</b> (the image file is not changed)"), form_));
    form->addRow(tr("Brightness"), brightness_);
    form->addRow(tr("Contrast"), contrast_);
    form->addRow(tr("Saturation"), saturation_);
    form->addRow(tr("Tint"), tint_);
    form->addRow(QString(), flipY_);
    form->addRow(QString(), reset);
    form->addRow(buttons);

    auto* split = new QSplitter(this);
    split->addWidget(left);
    split->addWidget(form_);
    split->setStretchFactor(0, 1);
    split->setSizes({700, 320});
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(split);

    for (QComboBox* c : {wrap_, filter_, maxSize_})
        connect(c, &QComboBox::currentIndexChanged, this, &TextureEditor::edited);
    for (QCheckBox* c : {mipmaps_, srgb_, flipY_, compress_})
        connect(c, &QCheckBox::toggled, this, &TextureEditor::edited);
    connect(zoom_, &QComboBox::currentIndexChanged, this, &TextureEditor::refreshPreview);
    connect(tile_, &QCheckBox::toggled, this, &TextureEditor::refreshPreview);
    connect(tint_, &QPushButton::clicked, this, [this] {
        if (!asset_)
            return;
        glm::vec3& c = asset_->settings.tint;
        const QColor picked = QColorDialog::getColor(QColor::fromRgbF(c.r, c.g, c.b), this, tr("Tint"));
        if (!picked.isValid())
            return;
        c = {static_cast<float>(picked.redF()), static_cast<float>(picked.greenF()), static_cast<float>(picked.blueF())};
        edited();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!asset_)
            return;
        engine::TextureSettings& s = asset_->settings;
        s.brightness = 0.0f;
        s.contrast = 1.0f;
        s.saturation = 1.0f;
        s.tint = glm::vec3(1.0f);
        s.flipY = false;
        refreshForm();
        edited();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        QString error;
        if (!this->save(&error))
            QMessageBox::warning(this, tr("Save texture settings"), error);
    });
    connect(revert, &QPushButton::clicked, this, &TextureEditor::revert);
    form_->setEnabled(false);
}

QString TextureEditor::path() const
{
    return asset_ ? QString::fromStdU16String(asset_->imagePath.u16string()) : QString();
}

QString TextureEditor::title() const
{
    return QFileInfo(path()).fileName() + (modified_ ? "*" : "");
}

bool TextureEditor::load(const QString& image, QString* error)
{
    std::string err;
    auto asset = engine::loadTextureAsset(std::filesystem::path(image.toStdU16String()), &err);
    if (!asset) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    asset_ = std::move(asset);
    if (asset_->texture && asset_->texture->rgba.empty())
        engine::applyTextureSettings(*asset_); // loaded from the cook cache: decode for the preview
    modified_ = false;
    form_->setEnabled(true);
    refreshForm();
    refreshPreview();
    return true;
}

bool TextureEditor::save(QString* error)
{
    if (!asset_)
        return true;
    std::string err;
    if (!engine::saveTextureSettings(*asset_, &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    engine::applyTextureSettings(*asset_, true); // cook (compress, size limit) as the game will see it
    modified_ = false;
    emit modificationChanged(false);
    refreshPreview();
    emit textureChanged();
    return true;
}

void TextureEditor::revert()
{
    if (!asset_)
        return;
    asset_->settings = engine::readTextureSettings(asset_->imagePath);
    engine::applyTextureSettings(*asset_);
    modified_ = false;
    emit modificationChanged(false);
    refreshForm();
    refreshPreview();
    emit textureChanged();
}

void TextureEditor::refreshForm()
{
    if (!asset_)
        return;
    loading_ = true;
    const engine::TextureSettings& s = asset_->settings;
    wrap_->setCurrentIndex(static_cast<int>(s.wrap));
    filter_->setCurrentIndex(s.nearest ? 1 : 0);
    mipmaps_->setChecked(s.mipmaps);
    srgb_->setChecked(s.srgb);
    flipY_->setChecked(s.flipY);
    brightness_->setValue(s.brightness);
    contrast_->setValue(s.contrast);
    saturation_->setValue(s.saturation);
    tint_->setStyleSheet(colorStyle(s.tint));
    compress_->setChecked(s.compress);
    maxSize_->setCurrentIndex(std::max(0, maxSize_->findData(s.maxSize)));
    loading_ = false;
}

void TextureEditor::edited()
{
    if (loading_ || !asset_)
        return;
    engine::TextureSettings& s = asset_->settings;
    s.wrap = static_cast<engine::TextureSettings::Wrap>(wrap_->currentIndex());
    s.nearest = filter_->currentIndex() == 1;
    s.mipmaps = mipmaps_->isChecked();
    s.srgb = srgb_->isChecked();
    s.flipY = flipY_->isChecked();
    s.brightness = static_cast<float>(brightness_->value());
    s.contrast = static_cast<float>(contrast_->value());
    s.saturation = static_cast<float>(saturation_->value());
    s.compress = compress_->isChecked();
    s.maxSize = maxSize_->currentData().toInt();
    tint_->setStyleSheet(colorStyle(s.tint));
    // Live preview without cooking (fast while dragging); Save cooks it.
    engine::applyTextureSettings(*asset_, false);
    if (!modified_) {
        modified_ = true;
        emit modificationChanged(true);
    }
    refreshPreview();
    emit textureChanged();
}

void TextureEditor::refreshPreview()
{
    if (!asset_ || !asset_->texture)
        return;
    const engine::Texture& t = *asset_->texture;
    if (t.rgba.empty())
        engine::applyTextureSettings(*asset_, !modified_); // the renderer released the pixels
    const int width = asset_->width, height = asset_->height;
    const QImage img(t.rgba.data(), width, height, width * 4, QImage::Format_RGBA8888);
    const int zoom = zoom_->currentData().toInt();
    const int tiles = tile_->isChecked() ? 3 : 1;
    const QSize cell(std::max(1, width * zoom / 100), std::max(1, height * zoom / 100));
    QPixmap pix(cell.width() * tiles, cell.height() * tiles);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    const Qt::TransformationMode mode = asset_->settings.nearest ? Qt::FastTransformation : Qt::SmoothTransformation;
    const QImage scaled = img.scaled(cell, Qt::IgnoreAspectRatio, mode);
    for (int y = 0; y < tiles; ++y)
        for (int x = 0; x < tiles; ++x) {
            QImage tileImg = scaled;
            if (asset_->settings.wrap == engine::TextureSettings::Wrap::Mirror)
                tileImg = scaled.mirrored(x % 2 == 1, y % 2 == 1);
            p.drawImage(x * cell.width(), y * cell.height(), tileImg);
        }
    p.end();
    image_->setPixmap(pix);
    const double mb = 1024.0 * 1024.0;
    const double plain = static_cast<double>(width) * height * 4 * 4 / 3 / mb;
    QString memory;
    if (modified_)
        memory = tr("GPU: cooked on Save");
    else
        memory = tr("GPU: %1 x %2 %3, %4 MB (plain RGBA %5 MB)")
                     .arg(t.width)
                     .arg(t.height)
                     .arg(engine::textureFormatName(t.format))
                     .arg(static_cast<double>(t.gpuBytes()) / mb, 0, 'f', 2)
                     .arg(plain, 0, 'f', 2);
    info_->setText(tr("%1  ·  %2 x %3 px  ·  %4  ·  settings: %5%6")
                       .arg(QDir::toNativeSeparators(path()))
                       .arg(width)
                       .arg(height)
                       .arg(memory)
                       .arg(QString::fromStdU16String(engine::textureSettingsPath(asset_->imagePath).filename().u16string()))
                       .arg(modified_ ? tr("  (unsaved)") : QString()));}
