#include "xmlreader.h"

#include "utility.h"
#include <QDebug>

using MOBase::MyException;

bool XmlReader::getNextElement(QString const &start)
{
  while (!atEnd()) {
    switch (readNext()) {
      case EndElement:
        if (name() != start) {
          qWarning() << "Got end of " << name() << ", expected " << start << " at " << lineNumber();
          /**/throw MyException("bad xml");
          continue;
        }
        return false;

      case StartElement:
        return true;

      case Invalid:
        return false;

      default:
        qWarning() << "Unexpected token type " << tokenString() << " at " << lineNumber();
        /**/throw MyException("bad xml");
    }
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
  /**/throw MyException("bad xml");
}

void XmlReader::finishedElement()
{
  QString const self = name().toString();
  while (!atEnd()) {
    switch (readNext()) {
      case EndElement:
        if (name() != self) {
          qWarning() << "Got end element for " << name() << ", expected " << self << " at " << lineNumber();
          /**/throw MyException("bad xml");
          continue;
        }
        return;

      case Invalid:
        return;

      case StartElement:
        unexpected();
        break;

      default:
        qWarning() << "Unexpected token type " << tokenString() << " at " << lineNumber();
        /**/throw MyException("bad xml");
    }
  }
}

QString XmlReader::getText()
{
  //This reads the text in an element, leaving you at the next element.
  QString result;
  while (QXmlStreamReader::readNext() == Comment || tokenType() == Characters) {
    if (tokenType() == Characters) {
      result += text();
    }
  }
  if (tokenType() != EndElement) {
      qWarning() << "Unexpected token type " << tokenString() << " at " << lineNumber();
      /**/throw MyException("bad xml");
  }
  return result;
}
