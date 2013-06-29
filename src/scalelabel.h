#ifndef SCALELABEL_H
#define SCALELABEL_H

#include <QLabel>

class ScaleLabel : public QLabel
{
  Q_OBJECT
public:
  explicit ScaleLabel(QWidget *parent = 0);

  void setScalablePixmap(const QPixmap &pixmap);
signals:
  
public slots:
protected:
  virtual void resizeEvent(QResizeEvent *event);
private:
  QPixmap m_Original;
};

#endif // SCALELABEL_H
