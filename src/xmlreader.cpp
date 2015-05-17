#include "xmlreader.h"

#include <QDebug>

bool XmlReader::getNextElement()
{
  while (!atEnd() && readNext() != EndElement) {
    if (tokenType() != StartElement) {
      qWarning() << "Unexpected token type " << tokenType() << " at " << lineNumber();
      continue;
    }
    return true;
  }
  return false;
}

void XmlReader::unexpected()
{
  qWarning() << "Unexpected element " << name() << " near line " << lineNumber();
  //Eat the contents
  QString s = readElementText(IncludeChildElements);
  //Print them out if in debugging mode
  qDebug() << " contains " << s;
}
