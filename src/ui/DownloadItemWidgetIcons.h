#pragma once

#include <QApplication>
#include <QIcon>
#include <QMap>
#include <QPainter>
#include <QPair>
#include <QStyle>
#include <QStyleOption>

namespace DownloadItemWidgetIcons {

inline QIcon createColoredIcon(QStyle::StandardPixmap standardPixmap, const QColor &color)
{
    static QMap<QPair<int, QRgb>, QIcon> cache;
    const QPair<int, QRgb> key = qMakePair(static_cast<int>(standardPixmap), color.rgba());
    if (cache.contains(key)) {
        return cache.value(key);
    }

    QPixmap pixmap = QApplication::style()->standardIcon(standardPixmap).pixmap(32, 32);
    if (pixmap.isNull()) {
        return {};
    }

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);

    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    QStyleOption styleOption;
    styleOption.palette = QApplication::palette();
    const QPixmap disabledPixmap = QApplication::style()->generatedIconPixmap(
        QIcon::Disabled, pixmap, &styleOption);
    if (!disabledPixmap.isNull()) {
        icon.addPixmap(disabledPixmap, QIcon::Disabled);
    }

    cache.insert(key, icon);
    return icon;
}

} // namespace DownloadItemWidgetIcons
