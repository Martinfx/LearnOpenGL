#pragma once
#include <QDialog>

#include <functional>
#include <memory>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;

// Assets > Validate assets: the asset registry's checks (engine/AssetRegistry.h) on a
// worker thread: references to missing files, unused assets and, optionally, models
// that do not fit the world scale. The same checks run in CI (asset_check).
// Double-click opens the asset.
class AssetValidationDialog : public QDialog {
    Q_OBJECT
public:
    explicit AssetValidationDialog(std::function<void(const QString&)> open, QWidget* parent = nullptr);
    ~AssetValidationDialog() override;

private:
    void start();
    void filter();

    std::function<void(const QString&)> open_;
    QCheckBox* models_ = nullptr;
    QCheckBox* unused_ = nullptr;
    QPushButton* run_ = nullptr;
    QLabel* status_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTimer* poll_ = nullptr;
    struct Job;
    std::shared_ptr<Job> job_;
};
