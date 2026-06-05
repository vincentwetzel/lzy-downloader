#pragma once

#include <QAbstractButton>
#include <QPropertyAnimation>
#include <QPainter>
#include <QEnterEvent>
#include <QColor>

class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(int m_offset READ offset WRITE setOffset)

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_offset;
    int m_handleRadius;
    int m_padding;
    QPropertyAnimation *m_animation;

    void setOffset(int offset);
    int offset() const;

    // Customizable colors
    QColor m_handleColor = Qt::white;
    QColor m_trackColorOff = QColor("#a0a0a0");
    QColor m_trackColorOn = QColor("#0078d7");
    QColor m_handleColorHover = QColor("#f0f0f0");
    QColor m_trackColorOffHover = QColor("#888888");
    QColor m_trackColorOnHover = QColor("#005a9e");
};

