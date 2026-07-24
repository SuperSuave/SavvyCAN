#include "thememanager.h"

#include <QStyleFactory>
#include <QtMath>

ThemeColors ThemeManager::colors()
{
    ThemeColors c;

    c.windowBg = QColor(45, 45, 45);
    c.panelBg = QColor(53, 53, 53);
    c.viewBg = QColor(30, 30, 30);
    c.altViewBg = QColor(37, 37, 37);
    c.border = QColor(90, 90, 90);
    c.gridLine = QColor(68, 68, 68);

    c.text = QColor(220, 220, 220);
    c.mutedText = QColor(168, 168, 168);
    c.disabledText = QColor(122, 122, 122);

    c.accent = QColor(64, 156, 255);
    c.selectionBg = QColor(64, 156, 255);
    c.selectionText = QColor(255, 255, 255);

    c.bitZeroBg = QColor(43, 43, 43);
    c.bitOneBg = QColor(31, 111, 235);
    c.changedBitBg = QColor(201, 122, 26);

    c.graphBg = QColor(27, 27, 27);
    c.axis = QColor(128, 128, 128);
    c.graphText = QColor(207, 207, 207);

    c.trace[0] = QColor(79, 193, 255);
    c.trace[1] = QColor(123, 216, 143);
    c.trace[2] = QColor(255, 209, 102);
    c.trace[3] = QColor(255, 107, 107);
    c.trace[4] = QColor(160, 174, 192);
    c.trace[5] = QColor(199, 146, 234);
    c.trace[6] = QColor(94, 234, 212);
    c.trace[7] = QColor(247, 140, 108);

    return c;
}

QPalette ThemeManager::darkPalette()
{
    const auto c = colors();
    QPalette pal;

    pal.setColor(QPalette::Window, c.windowBg);
    pal.setColor(QPalette::WindowText, c.text);
    pal.setColor(QPalette::Base, c.viewBg);
    pal.setColor(QPalette::AlternateBase, c.altViewBg);
    pal.setColor(QPalette::ToolTipBase, c.panelBg);
    pal.setColor(QPalette::ToolTipText, c.text);
    pal.setColor(QPalette::Text, c.text);
    pal.setColor(QPalette::Button, c.panelBg);
    pal.setColor(QPalette::ButtonText, c.text);
    pal.setColor(QPalette::BrightText, QColor(255, 128, 128));
    pal.setColor(QPalette::Link, c.accent);
    pal.setColor(QPalette::Highlight, c.selectionBg);
    pal.setColor(QPalette::HighlightedText, c.selectionText);
    pal.setColor(QPalette::PlaceholderText, c.mutedText);

    pal.setColor(QPalette::Disabled, QPalette::Text, c.disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabledText);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, c.disabledText);

    return pal;
}

QString ThemeManager::darkStyleSheet()
{
    const auto c = colors();

    return QString(R"(
        QToolTip {
            color: %1;
            background-color: %2;
            border: 1px solid %3;
        }

        QMenuBar, QToolBar, QStatusBar, QHeaderView::section {
            background-color: %2;
            color: %1;
        }

        QMenu {
            background-color: %2;
            color: %1;
            border: 1px solid %3;
        }

        QMenu::item:selected,
        QMenuBar::item:selected,
        QAbstractItemView::item:selected,
        QTabBar::tab:selected {
            background-color: %4;
            color: %5;
        }

        QTableView, QListView, QTreeView, QTextEdit, QPlainTextEdit, QLineEdit {
            background-color: %6;
            alternate-background-color: %7;
            color: %1;
            gridline-color: %8;
            border: 1px solid %3;
            selection-background-color: %4;
            selection-color: %5;
        }

        QTabWidget::pane {
            border: 1px solid %3;
            background: %6;
        }

        QTabBar::tab {
            background: %2;
            color: %1;
            border: 1px solid %3;
            padding: 6px 12px;
        }

        QTabBar::tab:!selected {
            background: %7;
        }
    )")
    .arg(c.text.name(),
         c.panelBg.name(),
         c.border.name(),
         c.selectionBg.name(),
         c.selectionText.name(),
         c.viewBg.name(),
         c.altViewBg.name(),
         c.gridLine.name());
}

void ThemeManager::applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setPalette(darkPalette());
    app.setStyleSheet(darkStyleSheet());
}

qreal ThemeManager::relativeLuminance(const QColor& c)
{
    auto channel = [](qreal v) {
        v /= 255.0;
        return (v <= 0.03928) ? (v / 12.92) : qPow((v + 0.055) / 1.055, 2.4);
    };

    const qreal r = channel(c.red());
    const qreal g = channel(c.green());
    const qreal b = channel(c.blue());

    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

qreal ThemeManager::contrastRatio(const QColor& a, const QColor& b)
{
    const qreal l1 = relativeLuminance(a);
    const qreal l2 = relativeLuminance(b);
    const qreal lighter = qMax(l1, l2);
    const qreal darker = qMin(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

QColor ThemeManager::adjustedForContrast(const QColor& fg,
                                         const QColor& bg,
                                         qreal minContrast)
{
    QColor candidate = fg;
    if (contrastRatio(candidate, bg) >= minContrast) return candidate;

    const bool bgIsDark = relativeLuminance(bg) < 0.5;

    for (int i = 0; i < 32; i++) {
        int h, s, l, a;
        candidate.getHsl(&h, &s, &l, &a);

        if (bgIsDark) l = qMin(255, l + 8);
        else          l = qMax(0, l - 8);

        candidate.setHsl(h, s, l, a);

        if (contrastRatio(candidate, bg) >= minContrast) return candidate;
    }

    return bgIsDark ? QColor("#f0f0f0") : QColor("#101010");
}