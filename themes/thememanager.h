#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>

struct ThemeColors
{
    QColor windowBg;
    QColor panelBg;
    QColor viewBg;
    QColor altViewBg;
    QColor border;
    QColor gridLine;

    QColor text;
    QColor mutedText;
    QColor disabledText;

    QColor accent;
    QColor selectionBg;
    QColor selectionText;

    QColor bitZeroBg;
    QColor bitOneBg;
    QColor changedBitBg;

    QColor graphBg;
    QColor axis;
    QColor graphText;
    QColor trace[8];
};

class ThemeManager
{
public:
    static void applyDarkTheme(QApplication& app);
    static ThemeColors colors();

    static QColor adjustedForContrast(const QColor& fg,
                                      const QColor& bg,
                                      qreal minContrast = 4.5);

    static qreal contrastRatio(const QColor& a, const QColor& b);

private:
    static QPalette darkPalette();
    static QString darkStyleSheet();
    static qreal relativeLuminance(const QColor& c);
};