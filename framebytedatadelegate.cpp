#include "framebytedatadelegate.h"
#include "canframemodel.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>

FrameByteDataDelegate::FrameByteDataDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FrameByteDataDelegate::paint(QPainter *painter,
                                  const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    if (!index.isValid())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const QString text = index.data(Qt::DisplayRole).toString();
    const QByteArray changedMask = index.data(CANFrameModel::ChangedBytesRole).toByteArray();

    if (text.isEmpty() || changedMask.isEmpty())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    painter->save();

    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    textRect.adjust(4, 0, -4, 0);

    const QStringList lines = text.split('\n');
    const QFontMetrics fm(opt.font);
    const int lineHeight = fm.height();

    QColor normalColor = opt.palette.color(QPalette::Text);
    QColor changedBg = opt.palette.color(QPalette::Highlight);
    changedBg.setAlpha(55);

    int y = textRect.top() + fm.ascent();

    for (int lineIdx = 0; lineIdx < lines.size(); ++lineIdx)
    {
        const QStringList bytes = lines[lineIdx].split(' ', Qt::SkipEmptyParts);
        int x = textRect.left();

        for (int i = 0; i < bytes.size(); ++i)
        {
            const int byteIndex = lineIdx * 8 + i;
            const QString byteText = bytes[i];
            const int tokenWidth = fm.horizontalAdvance(byteText);

            QRect tokenRect(x - 1, y - fm.ascent(), tokenWidth + 2, lineHeight);

            if (byteIndex < changedMask.size() && changedMask[byteIndex])
                painter->fillRect(tokenRect, changedBg);

            painter->setPen(normalColor);
            painter->drawText(x, y, byteText);

            x += tokenWidth + fm.horizontalAdvance(QStringLiteral(" "));
        }

        y += lineHeight;
    }

    painter->restore();
}