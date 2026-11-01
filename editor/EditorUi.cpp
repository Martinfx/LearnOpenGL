#include "EditorUi.h"

#include <QApplication>
#include <QLinearGradient>
#include <algorithm>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// All icons are drawn on a 16 x 16 grid (scaled up for sharp HiDPI pixmaps).
void drawIcon(QPainter& p, const QString& n, QColor c)
{
    QPen pen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto arrowHead = [&](QPointF tip, QPointF from) {
        const QPointF d = tip - from;
        const double len = std::hypot(d.x(), d.y());
        const QPointF u = d / len, side(-u.y(), u.x());
        QPolygonF head{tip, tip - u * 3.2 + side * 2.2, tip - u * 3.2 - side * 2.2};
        p.save();
        p.setBrush(c);
        p.drawPolygon(head);
        p.restore();
    };
    auto line = [&](double x1, double y1, double x2, double y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };

    if (n == "play") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(92, 196, 92));
        p.drawPolygon(QPolygonF{{4, 2.5}, {13.5, 8}, {4, 13.5}});
    } else if (n == "pause") {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(QRectF(3.5, 3, 3, 10));
        p.drawRect(QRectF(9.5, 3, 3, 10));
    } else if (n == "stop") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(222, 84, 74));
        p.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1, 1);
    } else if (n == "save") {
        p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1.5, 1.5);
        p.drawRect(QRectF(5, 2.5, 6, 4));
        p.drawRect(QRectF(4.5, 9, 7, 4.5));
    } else if (n == "undo" || n == "redo") {
        const bool undo = n == "undo";
        QPainterPath path;
        path.moveTo(undo ? 4 : 12, 7);
        path.cubicTo(undo ? 7 : 9, 3.5, undo ? 14 : 2, 4, undo ? 12.5 : 3.5, 12.5);
        p.drawPath(path);
        arrowHead(QPointF(undo ? 2.5 : 13.5, 8.5), QPointF(undo ? 5.5 : 10.5, 6));
    } else if (n == "add") {
        p.setPen(QPen(QColor(92, 196, 92), 2.0, Qt::SolidLine, Qt::RoundCap));
        line(8, 3, 8, 13);
        line(3, 8, 13, 8);
    } else if (n == "import") {
        line(8, 2, 8, 10);
        arrowHead(QPointF(8, 11), QPointF(8, 6));
        p.drawPolyline(QPolygonF{{2.5, 10}, {2.5, 13.5}, {13.5, 13.5}, {13.5, 10}});
    } else if (n == "select") {
        p.setBrush(c);
        p.drawPolygon(QPolygonF{{4, 2}, {4, 13}, {6.8, 10.3}, {9, 14.5}, {10.8, 13.6}, {8.7, 9.6}, {12.5, 9.6}});
    } else if (n == "move") {
        line(8, 2.5, 8, 13.5);
        line(2.5, 8, 13.5, 8);
        arrowHead({8, 1.5}, {8, 8});
        arrowHead({8, 14.5}, {8, 8});
        arrowHead({1.5, 8}, {8, 8});
        arrowHead({14.5, 8}, {8, 8});
    } else if (n == "rotate") {
        p.drawArc(QRectF(2.5, 2.5, 11, 11), 60 * 16, 290 * 16);
        arrowHead({11.2, 2.4}, {8.5, 2.2});
    } else if (n == "scale") {
        p.drawRect(QRectF(2.5, 7.5, 6, 6));
        line(8.5, 7.5, 13, 3);
        arrowHead({13.5, 2.5}, {9, 7});
        p.drawPolyline(QPolygonF{{8.5, 2.5}, {13.5, 2.5}, {13.5, 7.5}});
    } else if (n == "grid") {
        for (double v : {5.5, 10.5}) {
            line(v, 2, v, 14);
            line(2, v, 14, v);
        }
    } else if (n == "angle") {
        line(2.5, 13.5, 13.5, 13.5);
        line(2.5, 13.5, 11, 4);
        p.drawArc(QRectF(-3.5, 7.5, 12, 12), 0, 48 * 16);
    } else if (n == "scalesnap") {
        p.drawRect(QRectF(2.5, 2.5, 11, 11));
        p.drawRect(QRectF(2.5, 7.5, 6, 6));
    } else if (n == "camera") {
        p.drawRoundedRect(QRectF(1.5, 4.5, 9, 7), 1, 1);
        p.drawPolygon(QPolygonF{{10.5, 7}, {14.5, 5}, {14.5, 11}, {10.5, 9}});
    } else if (n == "menu") {
        for (double y : {4.0, 8.0, 12.0})
            line(3, y, 13, y);
    } else if (n == "lit") {
        p.drawEllipse(QPointF(8, 8), 3, 3);
        for (int i = 0; i < 8; ++i) {
            const double a = i * kPi / 4;
            line(8 + std::cos(a) * 5, 8 + std::sin(a) * 5, 8 + std::cos(a) * 6.8, 8 + std::sin(a) * 6.8);
        }
    } else if (n == "view" || n == "cube") {
        p.drawPolygon(QPolygonF{{8, 1.8}, {13.5, 4.8}, {13.5, 11.2}, {8, 14.2}, {2.5, 11.2}, {2.5, 4.8}});
        p.drawPolyline(QPolygonF{{2.5, 4.8}, {8, 7.8}, {13.5, 4.8}});
        line(8, 7.8, 8, 14.2);
    } else if (n == "folder") {
        p.setBrush(QColor(196, 160, 92));
        p.setPen(QPen(QColor(196, 160, 92).darker(140), 1));
        p.drawPolygon(QPolygonF{{1.5, 3.5}, {6, 3.5}, {7.5, 5}, {14.5, 5}, {14.5, 13}, {1.5, 13}});
    } else if (n == "star") {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double a = -kPi / 2 + i * kPi / 5;
            const double r = i % 2 ? 2.8 : 6.5;
            star << QPointF(8 + std::cos(a) * r, 8.5 + std::sin(a) * r);
        }
        p.setBrush(QColor(232, 196, 72));
        p.setPen(Qt::NoPen);
        p.drawPolygon(star);
    } else if (n == "search") {
        p.drawEllipse(QPointF(6.5, 6.5), 4, 4);
        line(9.5, 9.5, 13.5, 13.5);
    } else if (n == "person") {
        p.drawEllipse(QPointF(8, 4), 2.3, 2.3);
        p.drawPolyline(QPolygonF{{3.5, 14}, {4.5, 8.5}, {11.5, 8.5}, {12.5, 14}});
    } else if (n == "model") {
        p.drawPolygon(QPolygonF{{8, 2}, {14, 13}, {2, 13}});
        line(8, 2, 8, 13);
    } else if (n == "log") {
        p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1.5, 1.5);
        for (double y : {5.5, 8.0, 10.5})
            line(5, y, 11, y);
    } else if (n == "console") {
        p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.5, 1.5);
        p.drawPolyline(QPolygonF{{4, 6}, {6.5, 8}, {4, 10}});
        line(8, 10.5, 11.5, 10.5);
    } else if (n == "content") {
        for (double x : {2.5, 9.0})
            for (double y : {2.5, 9.0})
                p.drawRect(QRectF(x, y, 4.5, 4.5));
    } else if (n == "delete") {
        p.drawPolyline(QPolygonF{{3.5, 4.5}, {4.5, 14}, {11.5, 14}, {12.5, 4.5}});
        line(2, 4.5, 14, 4.5);
        line(6, 2.5, 10, 2.5);
        line(6.5, 7, 6.5, 11.5);
        line(9.5, 7, 9.5, 11.5);
    } else if (n == "duplicate") {
        p.drawRect(QRectF(2.5, 5.5, 8, 8));
        p.drawPolyline(QPolygonF{{5.5, 5.5}, {5.5, 2.5}, {13.5, 2.5}, {13.5, 10.5}, {10.5, 10.5}});
    } else if (n == "focus") {
        for (const QPointF& corner : {QPointF(2.5, 2.5), QPointF(13.5, 2.5), QPointF(2.5, 13.5), QPointF(13.5, 13.5)}) {
            const double sx = corner.x() < 8 ? 1 : -1, sy = corner.y() < 8 ? 1 : -1;
            p.drawPolyline(QPolygonF{corner + QPointF(0, sy * 3.5), corner, corner + QPointF(sx * 3.5, 0)});
        }
        p.drawEllipse(QPointF(8, 8), 2, 2);
    } else if (n == "floor") {
        p.drawRect(QRectF(5, 2.5, 6, 6));
        line(8, 9.5, 8, 11.5);
        arrowHead({8, 12.5}, {8, 9});
        line(1.5, 14, 14.5, 14);
    } else if (n == "landscape") {
        const QPointF hills[] = {{1.5, 13}, {5.5, 6}, {8, 9.5}, {10.5, 4}, {14.5, 13}};
        p.drawPolyline(hills, 5);
        line(1.5, 14, 14.5, 14);
    } else if (n == "brush") {
        const QPointF bristles[] = {{2, 14.5}, {2.5, 11}, {5, 9}, {7, 11}, {5.5, 13.5}};
        p.drawPolygon(bristles, 5);
        line(6, 10, 13.5, 2.5);
        line(7.5, 11.5, 14.5, 4.5);
    } else if (n == "foliage") {
        const QPointF tree[] = {{8, 1.5}, {12.5, 10}, {3.5, 10}};
        p.drawPolygon(tree, 3);
        line(8, 10, 8, 14.5);
        line(1.5, 14.5, 3, 12);
        line(14.5, 14.5, 13, 12);
    } else if (n == "particles") {
        for (const QPointF c : {QPointF(4, 11), QPointF(8, 6), QPointF(12, 10), QPointF(6, 3), QPointF(11, 3.5)})
            p.drawEllipse(c, 1.6, 1.6);
        p.drawEllipse(QPointF(8, 12.5), 2.4, 2.4);
    } else if (n == "sound") {
        const QPointF speaker[] = {{2, 6}, {5, 6}, {9, 2.5}, {9, 13.5}, {5, 10}, {2, 10}};
        p.drawPolygon(speaker, 6);
        p.drawArc(QRectF(8, 5, 5, 6), -60 * 16, 120 * 16);
        p.drawArc(QRectF(8, 2.5, 7.5, 11), -60 * 16, 120 * 16);
    } else if (n == "settings") {
        p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
        p.drawEllipse(QPointF(8, 8), 5, 5);
        for (int i = 0; i < 6; ++i) {
            const double a = i * kPi / 3;
            line(8 + std::cos(a) * 5, 8 + std::sin(a) * 5, 8 + std::cos(a) * 6.8, 8 + std::sin(a) * 6.8);
        }
    } else {
        p.drawRect(QRectF(3.5, 3.5, 9, 9));
    }
}

} // namespace

QIcon editorIcon(const QString& name, QColor color)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + color.name();
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return *it;
    QIcon icon;
    for (int size : {16, 32, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 16.0, size / 16.0);
        drawIcon(p, name, color);
        p.end();
        icon.addPixmap(pixmap);
        QPixmap disabled(size, size);
        disabled.fill(Qt::transparent);
        QPainter d(&disabled);
        d.setRenderHint(QPainter::Antialiasing);
        d.scale(size / 16.0, size / 16.0);
        d.setOpacity(0.35);
        drawIcon(d, name, color);
        d.end();
        icon.addPixmap(disabled, QIcon::Disabled);
    }
    cache.insert(key, icon);
    return icon;
}

void applyEditorTheme()
{
    // Dark UI with a blue accent, same on every platform.
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    const QColor window(36, 37, 40), base(24, 25, 27), text(214, 216, 220), accent(38, 120, 220);
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, QColor(30, 31, 34));
    pal.setColor(QPalette::ToolTipBase, QColor(52, 54, 58));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor(48, 50, 54));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Link, accent.lighter(130));
    pal.setColor(QPalette::PlaceholderText, QColor(120, 123, 128));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, QColor(108, 110, 114));
    pal.setColor(QPalette::Disabled, QPalette::Base, QColor(32, 33, 36));
    QApplication::setPalette(pal);
    qApp->setStyleSheet(R"(
        QMainWindow::separator { background: #151618; width: 3px; height: 3px; }
        QDockWidget { titlebar-close-icon: none; }
        QDockWidget::title { background: #1d1e21; padding: 5px 8px; font-weight: bold; border-bottom: 1px solid #151618; }
        QToolBar { background: #242528; border: none; border-bottom: 1px solid #151618; spacing: 2px; padding: 3px; }
        QToolBar::separator { background: #3a3c40; width: 1px; margin: 4px 6px; }
        QToolButton { padding: 3px 6px; border-radius: 3px; border: 1px solid transparent; }
        QToolButton:hover { background: #3a3c41; border-color: #46484d; }
        QToolButton:checked { background: #2a5d9c; border-color: #2f7bd8; color: white; }
        QToolButton:disabled { color: #6c6e72; }
        QToolButton[popupMode="1"] { padding-right: 14px; }
        QPushButton { background: #303236; border: 1px solid #16171a; border-radius: 3px; padding: 4px 10px; }
        QPushButton:hover { background: #3a3c41; }
        QPushButton:pressed { background: #2a5d9c; }
        QPushButton:disabled { color: #6c6e72; background: #2a2b2e; }
        QPushButton#accent { background: #2a5d9c; border-color: #2f7bd8; color: white; }
        QPushButton#tool { text-align: left; padding: 5px 8px; }
        QLineEdit, QAbstractSpinBox, QComboBox { background: #141517; border: 1px solid #3a3c40; border-radius: 3px; padding: 2px 4px; }
        QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: #2f7bd8; }
        QComboBox::drop-down { border: none; }
        QTabBar::tab { background: #242528; padding: 5px 12px; border: none; color: #a8abb0; }
        QTabBar::tab:selected { background: #18191b; color: white; border-bottom: 2px solid #2f7bd8; }
        QTabBar::tab:hover { color: white; }
        QTabBar#categories::tab { padding: 2px 5px; margin-right: 2px; border-radius: 3px; background: #2c2e32; }
        QTabBar#categories::tab:selected { background: #2a5d9c; border: none; }
        QHeaderView::section { background: #242528; border: none; border-right: 1px solid #151618; padding: 4px; color: #a8abb0; }
        QTreeView, QListView, QTableView, QPlainTextEdit { border: none; background: #18191b; }
        QTreeView::item:hover, QListView::item:hover { background: #2c2e32; }
        QScrollBar:vertical { background: #18191b; width: 10px; }
        QScrollBar::handle:vertical { background: #44464b; border-radius: 4px; min-height: 24px; }
        QScrollBar:horizontal { background: #18191b; height: 10px; }
        QScrollBar::handle:horizontal { background: #44464b; border-radius: 4px; min-width: 24px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QStatusBar { background: #1d1e21; border-top: 1px solid #151618; }
        QStatusBar::item { border: none; }
        QToolButton#sectionHeader { background: #2c2e32; border: none; border-radius: 0; font-weight: bold; text-align: left; padding: 5px 4px; }
        QToolButton#sectionHeader:hover { background: #34363a; }
        QFrame#floatingPanel { background: rgba(28, 29, 32, 225); border: 1px solid #3f4146; border-radius: 4px; }
        QLabel#dim { color: #8d9096; }
        QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #5a5d63; border-radius: 2px; background: #141517; }
        QCheckBox::indicator:checked { background: #2f7bd8; border-color: #2f7bd8; }
    )");
}

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent) : QWidget(parent)
{
    header_ = new QToolButton(this);
    header_->setObjectName("sectionHeader");
    header_->setText(title);
    header_->setCheckable(true);
    header_->setChecked(true);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setArrowType(Qt::DownArrow);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    body_ = new QWidget(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 2);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(body_);
    connect(header_, &QToolButton::toggled, this, &CollapsibleSection::setExpanded);
}

void CollapsibleSection::setBodyLayout(QLayout* layout)
{
    layout->setContentsMargins(8, 4, 4, 4);
    body_->setLayout(layout);
}

void CollapsibleSection::setExpanded(bool expanded)
{
    header_->setChecked(expanded);
    header_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    body_->setVisible(expanded);
}

bool CollapsibleSection::expanded() const { return header_->isChecked(); }

QString CollapsibleSection::title() const { return header_->text(); }

// ---------------------------------------------------------------------------
EditorSplash::EditorSplash(const QString& backgroundImage)
{
    QPixmap pixmap(720, 405);
    pixmap.fill(QColor(16, 15, 14));
    QPixmap image(backgroundImage);
    if (!image.isNull()) {
        QPainter p(&pixmap);
        p.drawPixmap(pixmap.rect(), image.scaled(pixmap.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        // Darken toward the bottom where the text goes.
        QLinearGradient shade(0, 0, 0, pixmap.height());
        shade.setColorAt(0.0, QColor(10, 9, 8, 60));
        shade.setColorAt(0.55, QColor(10, 9, 8, 120));
        shade.setColorAt(1.0, QColor(10, 9, 8, 235));
        p.fillRect(pixmap.rect(), shade);
    }
    setPixmap(pixmap);
}

void EditorSplash::setProgress(int percent, const QString& step)
{
    percent_ = std::clamp(percent, 0, 100);
    step_ = step;
    repaint();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}

void EditorSplash::drawContents(QPainter* p)
{
    const QRect r = rect();
    const int margin = 32;
    p->setRenderHint(QPainter::Antialiasing);
    QFont title = p->font();
    title.setPixelSize(34);
    title.setBold(true);
    p->setFont(title);
    p->setPen(QColor(222, 212, 188));
    p->drawText(QRect(margin, r.height() - 150, r.width() - 2 * margin, 44), Qt::AlignLeft | Qt::AlignBottom,
                QStringLiteral("FPS Engine Editor"));
    QFont small = p->font();
    small.setPixelSize(13);
    small.setBold(false);
    p->setFont(small);
    const QRect bar(margin, r.height() - 80, r.width() - 2 * margin, 8);
    p->fillRect(bar.adjusted(-2, -2, 2, 2), QColor(0, 0, 0, 160));
    p->fillRect(bar, QColor(42, 39, 34));
    p->fillRect(QRect(bar.left(), bar.top(), bar.width() * percent_ / 100, bar.height()), QColor(199, 107, 33));
    p->setPen(QColor(160, 154, 140));
    p->drawText(QRect(margin, bar.bottom() + 10, r.width() - 2 * margin - 60, 20), Qt::AlignLeft | Qt::AlignTop,
                step_.isEmpty() ? QString() : step_);
    p->setPen(QColor(222, 212, 188));
    p->drawText(QRect(margin, bar.bottom() + 10, r.width() - 2 * margin, 20), Qt::AlignRight | Qt::AlignTop,
                QStringLiteral("%1 %").arg(percent_));
}
