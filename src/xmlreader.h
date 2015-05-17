#ifndef XMLREADER_H
#define XMLREADER_H

#include <QXmlStreamReader>

class XmlReader : public QXmlStreamReader {
 public:
  XmlReader(QIODevice *device) :
    QXmlStreamReader(device)
  { }

  XmlReader(QByteArray array) :
    QXmlStreamReader(array)
  { }

  TokenType readNext()
  {
    while (QXmlStreamReader::readNext() == Comment || isWhitespace()) {
      continue;
    }
    return tokenType();
  }

  bool getNextElement();
  void unexpected();
};


#endif // XMLREADER_H
