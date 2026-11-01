#pragma once
#include <QDialog>

#include <functional>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTreeWidget;

// Assets > Check albedo palette: scans every material and glTF model under assets/
// on worker threads and lists base colors outside the plausible range (too dark,
// too bright, too saturated for the muted zone look). Double-click opens the asset.
class PaletteDialog : public QDialog {
    Q_OBJECT
public:
    explicit PaletteDialog(std::function<void(const QString&)> open, QWidget* parent = nullptr);
    ~PaletteDialog() override;

private:
    void start();
    void filter();

    std::function<void(const QString&)> open_;
    QSpinBox* darkest_ = nullptr;
    QSpinBox* brightest_ = nullptr;
    QDoubleSpinBox* share_ = nullptr;
    QDoubleSpinBox* saturation_ = nullptr;
    QCheckBox* onlyProblems_ = nullptr;
    QPushButton* run_ = nullptr;
    QLabel* status_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    std::shared_ptr<int> generation_; // results of an older scan are dropped
    int total_ = 0, done_ = 0, problems_ = 0;
};
