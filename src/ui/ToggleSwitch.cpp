#include "ToggleSwitch.h"
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPropertyAnimation>
#include <QPainter>

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent), m_offset(0), m_handleRadius(8), m_padding(2) {
    setCheckable(true);
    setFixedSize(50, 24);
    m_offset = m_padding + m_handleRadius;

    m_animation = new QPropertyAnimation(this, QByteArrayLiteral("m_offset"), this);
    m_animation->setDuration(120);
    m_animation->setEasingCurve(QEasingCurve::InOutSine);

    connect(this, &ToggleSwitch::toggled, this, [this](bool checked) {
        int start = m_padding + m_handleRadius;
        int end = width() - m_handleRadius - m_padding;
        m_animation->setStartValue(checked ? start : end);
        m_animation->setEndValue(checked ? end : start);
        m_animation->start();
    });
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(50, 24);
}

void ToggleSwitch::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    
    // Ensure offset matches current state (handles programmatic setChecked)
    int start = m_padding + m_handleRadius;
    int end = width() - m_handleRadius - m_padding;
    int targetOffset = isChecked() ? end : start;
    if (m_offset != targetOffset && m_animation->state() != QAbstractAnimation::Running) {
        m_offset = targetOffset;
    }
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor trackColor, handleColor;
    bool isHovered = underMouse();

    if (isChecked()) {
        trackColor = isHovered ? m_trackColorOnHover : m_trackColorOn;
    } else {
        trackColor = isHovered ? m_trackColorOffHover : m_trackColorOff;
    }
    handleColor = isHovered ? m_handleColorHover : m_handleColor;

    painter.setPen(Qt::NoPen);

    // Draw track
    painter.setBrush(trackColor);
    int trackHeight = height() - 2 * m_padding;
    int trackRadius = trackHeight / 2;
    painter.drawRoundedRect(m_padding, m_padding, width() - 2 * m_padding, trackHeight, trackRadius, trackRadius);

    // Draw handle
    painter.setBrush(handleColor);
    painter.drawEllipse(QPoint(m_offset, height() / 2), m_handleRadius, m_handleRadius);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent *event) {
    QAbstractButton::mouseReleaseEvent(event);
}

void ToggleSwitch::enterEvent(QEnterEvent *event) {
    update();
    QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent *event) {
    update();
    QAbstractButton::leaveEvent(event);
}

void ToggleSwitch::setOffset(int offset) {
    if (m_offset != offset) {
        m_offset = offset;
        update();
    }
}

int ToggleSwitch::offset() const {
    return m_offset;
}
