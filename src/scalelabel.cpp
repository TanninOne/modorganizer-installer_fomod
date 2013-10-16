#include "scalelabel.h"
#include <QResizeEvent>

ScaleLabel::ScaleLabel(QWidget *parent)
  : QLabel(parent)
{
}

void ScaleLabel::setScalablePixmap(const QPixmap &pixmap)
{
  m_Original = pixmap;
  setPixmap(m_Original.scaled(size(), Qt::KeepAspectRatio));
}

void ScaleLabel::resizeEvent(QResizeEvent *event)
{
  if ((pixmap() != NULL) && !pixmap()->isNull() && !m_Original.isNull()) {
    setPixmap(m_Original.scaled(event->size(), Qt::KeepAspectRatio));
  }
}
