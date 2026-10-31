#pragma once
#include <QColor>
#include <QIcon>
#include <QSplashScreen>
#include <QWidget>

class QToolButton;
class QVBoxLayout;

// Small line icons drawn with QPainter (no image files), tinted for the dark theme.
// Names: play pause stop save undo redo add import select move rotate scale grid angle
// scalesnap camera menu lit view folder star search cube person model log console
// content delete duplicate focus floor settings
QIcon editorIcon(const QString& name, QColor color = QColor(205, 208, 214));

// Dark theme shared by every window of the editor (Fusion + palette + style sheet).
void applyEditorTheme();

// Titled section that folds open and closed (Details, modeling tools).
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    QWidget* body() const { return body_; }
    void setBodyLayout(QLayout* layout);
    void setExpanded(bool expanded);
    bool expanded() const;
    QString title() const;

private:
    QToolButton* header_;
    QWidget* body_;
};

// Start-up screen: background image, title, progress bar and the current step.
class EditorSplash : public QSplashScreen {
    Q_OBJECT
public:
    explicit EditorSplash(const QString& backgroundImage);
    void setProgress(int percent, const QString& step);

protected:
    void drawContents(QPainter* painter) override;

private:
    int percent_ = 0;
    QString step_;
};
