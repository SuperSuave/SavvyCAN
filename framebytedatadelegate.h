#ifndef FRAMEBYTEDATADELEGATE_H
#define FRAMEBYTEDATADELEGATE_H

#include <QStyledItemDelegate>

class FrameByteDataDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FrameByteDataDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

#endif // FRAMEBYTEDATADELEGATE_H