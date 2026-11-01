#include "AssetValidationDialog.h"
#include "AssetPaths.h"

#include <engine/AssetRegistry.h>

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <atomic>
#include <mutex>
#include <thread>

struct AssetValidationDialog::Job {
    std::atomic<float> progress{0.0f};
    std::atomic<bool> done{false};
    std::mutex mutex;
    std::vector<engine::AssetIssue> issues;
    std::size_t assets = 0;
    std::string error;
};

AssetValidationDialog::AssetValidationDialog(std::function<void(const QString&)> open, QWidget* parent)
    : QDialog(parent), open_(std::move(open))
{
    setWindowTitle(tr("Validate assets"));
    resize(980, 560);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Checks every asset in assets/: references to files that do not exist (levels, materials, glTF "
                                "textures, .weapon, .particle, .sound, scripts), assets nothing uses, and with \"Model scale\" "
                                "glTF models against the world scale (1 unit = 1 m). CI runs the same checks (asset_check)."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* row = new QHBoxLayout();
    models_ = new QCheckBox(tr("Model scale (loads every model)"), this);
    unused_ = new QCheckBox(tr("Show unused assets"), this);
    run_ = new QPushButton(tr("Check"), this);
    row->addWidget(models_);
    row->addWidget(unused_);
    row->addStretch(1);
    row->addWidget(run_);
    layout->addLayout(row);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Severity"), tr("Asset"), tr("Problem")});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true); // listed errors first, then warnings, then unused
    tree_->header()->setSectionResizeMode(QHeaderView::Interactive);
    tree_->setColumnWidth(0, 80);
    tree_->setColumnWidth(1, 420);
    layout->addWidget(tree_, 1);
    status_ = new QLabel(this);
    layout->addWidget(status_);
    poll_ = new QTimer(this);
    poll_->setInterval(100);
    connect(run_, &QPushButton::clicked, this, &AssetValidationDialog::start);
    connect(unused_, &QCheckBox::toggled, this, &AssetValidationDialog::filter);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (open_)
            open_(item->data(1, Qt::UserRole).toString());
    });
    connect(poll_, &QTimer::timeout, this, [this] {
        if (!job_)
            return;
        if (!job_->done) {
            status_->setText(tr("Checking... %1 %").arg(int(job_->progress * 100.0f)));
            return;
        }
        poll_->stop();
        run_->setEnabled(true);
        tree_->clear();
        int errors = 0, warnings = 0, infos = 0;
        std::lock_guard<std::mutex> lock(job_->mutex);
        const QDir root(assetRoot());
        for (const engine::AssetIssue& issue : job_->issues) {
            auto* item = new QTreeWidgetItem(tree_);
            const QString file = QString::fromStdU16String(issue.file.u16string());
            const char* severity = issue.severity == engine::AssetIssue::Severity::Error     ? "Error"
                                   : issue.severity == engine::AssetIssue::Severity::Warning ? "Warning"
                                                                                             : "Unused";
            item->setText(0, tr(severity));
            item->setText(1, root.relativeFilePath(file));
            item->setData(1, Qt::UserRole, file);
            item->setText(2, QString::fromStdString(issue.message));
            item->setData(0, Qt::UserRole, static_cast<int>(issue.severity));
            const QColor color = issue.severity == engine::AssetIssue::Severity::Error     ? QColor(240, 90, 80)
                                 : issue.severity == engine::AssetIssue::Severity::Warning ? QColor(235, 190, 70)
                                                                                           : palette().color(QPalette::PlaceholderText);
            item->setForeground(0, color);
            errors += issue.severity == engine::AssetIssue::Severity::Error;
            warnings += issue.severity == engine::AssetIssue::Severity::Warning;
            infos += issue.severity == engine::AssetIssue::Severity::Info;
        }
        status_->setText(job_->error.empty() ? tr("%1 assets: %2 errors, %3 warnings, %4 unused")
                                                   .arg(job_->assets)
                                                   .arg(errors)
                                                   .arg(warnings)
                                                   .arg(infos)
                                             : QString::fromStdString(job_->error));
        filter();
    });
    start();
}

AssetValidationDialog::~AssetValidationDialog() = default;

void AssetValidationDialog::start()
{
    run_->setEnabled(false);
    tree_->clear();
    auto job = std::make_shared<Job>();
    job_ = job;
    const std::filesystem::path root(assetRoot().toStdU16String());
    const bool models = models_->isChecked();
    std::thread([job, root, models] { // the dialog may close first: the job owns its data
        engine::AssetRegistry registry;
        std::string error;
        std::vector<engine::AssetIssue> issues;
        if (registry.scan(root, &error, &job->progress))
            issues = registry.validate(models, models ? &job->progress : nullptr);
        std::lock_guard<std::mutex> lock(job->mutex);
        job->issues = std::move(issues);
        job->assets = registry.records().size();
        job->error = error;
        job->done = true;
    }).detach();
    poll_->start();
}

void AssetValidationDialog::filter()
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree_->topLevelItem(i);
        item->setHidden(!unused_->isChecked() && item->data(0, Qt::UserRole).toInt() == static_cast<int>(engine::AssetIssue::Severity::Info));
    }
}
