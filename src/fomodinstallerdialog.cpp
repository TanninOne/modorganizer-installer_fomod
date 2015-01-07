/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fomodinstallerdialog.h"
#include "report.h"
#include "utility.h"
#include "ui_fomodinstallerdialog.h"

#include <QFile>
#include <QDir>
#include <QImage>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QTextCodec>
#include <Shellapi.h>
#include <boost/assign.hpp>


using namespace MOBase;


bool ControlsAscending(QAbstractButton *LHS, QAbstractButton *RHS)
{
  return LHS->text() < RHS->text();
}


bool ControlsDescending(QAbstractButton *LHS, QAbstractButton *RHS)
{
  return LHS->text() > RHS->text();
}


bool PagesAscending(QGroupBox *LHS, QGroupBox *RHS)
{
  return LHS->title() < RHS->title();
}


bool PagesDescending(QGroupBox *LHS, QGroupBox *RHS)
{
  return LHS->title() > RHS->title();
}


FomodInstallerDialog::FomodInstallerDialog(const GuessedValue<QString> &modName, const QString &fomodPath,
                                           const std::function<MOBase::IPluginList::PluginState(const QString &)> &fileCheck,
                                           QWidget *parent)
  : QDialog(parent), ui(new Ui::FomodInstallerDialog), m_ModName(modName), m_ModID(-1),
    m_FomodPath(fomodPath), m_Manual(false), m_FileCheck(fileCheck)
{
  ui->setupUi(this);

  setWindowTitle(modName);

  updateNameEdit();
  ui->nameCombo->setAutoCompletionCaseSensitivity(Qt::CaseSensitive);
}

FomodInstallerDialog::~FomodInstallerDialog()
{
  delete ui;
}


bool FomodInstallerDialog::hasOptions()
{
  return ui->stepsStack->count() > 0;
}


void FomodInstallerDialog::updateNameEdit()
{
  ui->nameCombo->clear();
  for (auto iter = m_ModName.variants().begin(); iter != m_ModName.variants().end(); ++iter) {
    ui->nameCombo->addItem(*iter);
  }

  ui->nameCombo->setCurrentIndex(ui->nameCombo->findText(m_ModName));
}


int FomodInstallerDialog::bomOffset(const QByteArray &buffer)
{
  static const unsigned char BOM_UTF8[] = { 0xEF, 0xBB, 0xBF };
  static const unsigned char BOM_UTF16BE[] = { 0xFE, 0xFF };
  static const unsigned char BOM_UTF16LE[] = { 0xFF, 0xFE };

  if (buffer.startsWith(reinterpret_cast<const char*>(BOM_UTF8))) return 3;
  if (buffer.startsWith(reinterpret_cast<const char*>(BOM_UTF16BE)) ||
      buffer.startsWith(reinterpret_cast<const char*>(BOM_UTF16LE))) return 2;

  return 0;
}

#pragma message("implement module dependencies->file dependencies")


struct XmlParseError : std::runtime_error {
  XmlParseError(const QString &message)
    : std::runtime_error(message.toUtf8().constData()) {}
};

QByteArray skipXmlHeader(QIODevice &file)
{
  static const unsigned char UTF16LE_BOM[] = { 0xFF, 0xFE };
  static const unsigned char UTF16BE_BOM[] = { 0xFE, 0xFF };
  static const unsigned char UTF8_BOM[]    = { 0xEF, 0xBB, 0xBF };
  static const unsigned char UTF16LE[]     = { 0x3C, 0x00, 0x3F, 0x00 };
  static const unsigned char UTF16BE[]     = { 0x00, 0x3C, 0x00, 0x3F };
  static const unsigned char UTF8[]        = { 0x3C, 0x3F, 0x78, 0x6D };

  file.seek(0);
  QByteArray rawBytes = file.read(4);
  QTextStream stream(&file);
  int bom = 0;
  if (rawBytes.startsWith((const char*)UTF16LE_BOM)) {
    stream.setCodec("UTF16-LE");
    bom = 2;
  } else if (rawBytes.startsWith((const char*)UTF16BE_BOM)) {
    stream.setCodec("UTF16-BE");
    bom = 2;
  } else if (rawBytes.startsWith((const char*)UTF8_BOM)) {
    stream.setCodec("UTF-8");
    bom = 3;
  } else if (rawBytes.startsWith(QByteArray((const char *)UTF16LE, 4))) {
    stream.setCodec("UTF16-LE");
  } else if (rawBytes.startsWith(QByteArray((const char *)UTF16BE, 4))) {
    stream.setCodec("UTF16-BE");
  } else if (rawBytes.startsWith(QByteArray((const char*)UTF8, 4))) {
    stream.setCodec("UTF-8");
  } // otherwise maybe the textstream knows the encoding?

  stream.seek(bom);
  QString header = stream.readLine();
  if (!header.startsWith("<?")) {
    // it was all for nothing, there is no header here...
    stream.seek(bom);
  }
  // this seems to be necessary due to buffering in QTextStream
  file.seek(stream.pos());
  return file.readAll();
}

void FomodInstallerDialog::readInfoXml()
{
  QFile file(QDir::tempPath() + "/" + m_FomodPath + "/fomod/info.xml");
  if (file.open(QIODevice::ReadOnly)) {
    bool success = false;
    std::string errorMessage;
    try {
      QXmlStreamReader reader(&file);
      parseInfo(reader);
      success = true;
    } catch (const XmlParseError &e) {
      errorMessage = e.what();
    }

    if (!success) {
      // nmm's xml parser is less strict than the one from qt and allows files with
      // wrong encoding in the header. Being strict here would be bad user experience
      // this works around bad headers.
      QByteArray headerlessData = skipXmlHeader(file);

      // try parsing the file with several encodings to support broken files
      foreach (const char *encoding, boost::assign::list_of("utf-16")("utf-8")("iso-8859-1")) {
        qDebug("trying encoding %s", encoding);
        try {
          QTextCodec *codec = QTextCodec::codecForName(encoding);
          QXmlStreamReader reader(codec->fromUnicode(QString("<?xml version=\"1.0\" encoding=\"%1\" ?>").arg(encoding)) + headerlessData);
          parseInfo(reader);
          qDebug("interpreting as %s", encoding);
          success = true;
          break;
        } catch (const XmlParseError &e) {
          qDebug("not %s: %s", encoding, e.what());
        }
      }
      if (!success) {
        reportError(tr("Failed to parse ModuleConfig.xml. See console for details"));
      }
    }
    file.close();
  }
}

void FomodInstallerDialog::readModuleConfigXml()
{
  QFile file(QDir::tempPath() + "/" + m_FomodPath + "/fomod/ModuleConfig.xml");
  if (!file.open(QIODevice::ReadOnly)) {
    throw MyException(tr("ModuleConfig.xml missing"));
  }

  bool success = false;
  std::string errorMessage;
  try {
    QXmlStreamReader reader(&file);
    parseModuleConfig(reader);
    success = true;
  } catch (const XmlParseError &e) {
    qWarning("the ModuleConfig.xml in this file is incorrectly encoded (%s). Applying heuristics...", e.what());
  }

  if (!success) {
    // nmm's xml parser is less strict than the one from qt and allows files with
    // wrong encoding in the header. Being strict here would be bad user experience
    // this works around bad headers.
    QByteArray headerlessData = skipXmlHeader(file);

    // try parsing the file with several encodings to support broken files
    foreach (const char *encoding, boost::assign::list_of("utf-16")("utf-8")("iso-8859-1")) {
      try {
        QTextCodec *codec = QTextCodec::codecForName(encoding);
        QXmlStreamReader reader(codec->fromUnicode(QString("<?xml version=\"1.0\" encoding=\"%1\" ?>").arg(encoding)) + headerlessData);
        parseModuleConfig(reader);
        qDebug("interpreting as %s", encoding);
        success = true;
        break;
      } catch (const XmlParseError &e) {
        qDebug("not %s: %s", encoding, e.what());
      }
    }
    if (!success) {
      reportError(tr("Failed to parse ModuleConfig.xml. See console for details"));
    }

    file.close();
  }
}

void FomodInstallerDialog::initData()
{
  // parse provided package information
  readInfoXml();

  QImage screenshot(QDir::tempPath() + "/" + m_FomodPath + "/fomod/screenshot.png");
  if (!screenshot.isNull()) {
    ui->screenshotLabel->setScalablePixmap(QPixmap::fromImage(screenshot));
  }

  readModuleConfigXml();
}

QString FomodInstallerDialog::getName() const
{
  return ui->nameCombo->currentText();
}

QString FomodInstallerDialog::getVersion() const
{
  return ui->versionLabel->text();
}

int FomodInstallerDialog::getModID() const
{
  return m_ModID;
}

void FomodInstallerDialog::moveTree(DirectoryTree::Node *target, DirectoryTree::Node *source)
{
  for (DirectoryTree::const_node_iterator iter = source->nodesBegin(); iter != source->nodesEnd();) {
    target->addNode(*iter, true);
    iter = source->detach(iter);
  }

  for (DirectoryTree::const_leaf_reverse_iterator iter = source->leafsRBegin();
       iter != source->leafsREnd(); ++iter) {
    target->addLeaf(*iter);
  }
}


DirectoryTree::Node *FomodInstallerDialog::findNode(DirectoryTree::Node *node, const QString &path, bool create)
{
  if (path.length() == 0) {
    return node;
  }

  int pos = path.indexOf('\\');
  if (pos == -1) {
    pos = path.indexOf('/');
  }
  QString subPath = path;
  if (pos > 0) {
    subPath = path.mid(0, pos);
  }
  for (DirectoryTree::const_node_iterator iter = node->nodesBegin(); iter != node->nodesEnd(); ++iter) {
    if ((*iter)->getData().name.compare(subPath, Qt::CaseInsensitive) == 0) {
      if (pos <= 0) {
        return *iter;
      } else {
        return findNode(*iter, path.mid(pos + 1), create);
      }
    }
  }
  if (create) {
    DirectoryTree::Node *newNode = new DirectoryTree::Node;
    newNode->setData(subPath);
    node->addNode(newNode, false);
    if (pos <= 0) {
      return newNode;
    } else {
      return findNode(newNode, path.mid(pos + 1), create);
    }
  } else {
    throw MyException(QString("%1 not found in archive").arg(path));
  }
}

void FomodInstallerDialog::copyLeaf(DirectoryTree::Node *sourceTree, const QString &sourcePath,
                                    DirectoryTree::Node *destinationTree, const QString &destinationPath)
{
  int sourceFileIndex = sourcePath.lastIndexOf('\\');
  if (sourceFileIndex == -1) {
    sourceFileIndex = sourcePath.lastIndexOf('/');
    if (sourceFileIndex == -1) {
      sourceFileIndex = 0;
    }
  }
  DirectoryTree::Node *sourceNode = sourceFileIndex == 0 ? sourceTree : findNode(sourceTree, sourcePath.mid(0, sourceFileIndex), false);

  int destinationFileIndex = destinationPath.lastIndexOf('\\');
  if (destinationFileIndex == -1) {
    destinationFileIndex = destinationPath.lastIndexOf('/');
    if (destinationFileIndex == -1) {
      destinationFileIndex = 0;
    }
  }

  DirectoryTree::Node *destinationNode =
      destinationFileIndex == 0 ? destinationTree
                                : findNode(destinationTree, destinationPath.mid(0, destinationFileIndex), true);

  QString sourceName = sourcePath.mid((sourceFileIndex != 0) ? sourceFileIndex + 1 : 0);
  QString destinationName = (destinationFileIndex != 0) ? destinationPath.mid(destinationFileIndex + 1) : destinationPath;
  if (destinationName.length() == 0) {
    destinationName = sourceName;
  }

  bool found = false;
  for (DirectoryTree::const_leaf_reverse_iterator iter = sourceNode->leafsRBegin();
       iter != sourceNode->leafsREnd(); ++iter) {
    if (iter->getName().compare(sourceName, Qt::CaseInsensitive) == 0) {
      FileTreeInformation temp = *iter;
      temp.setName(destinationName);
      destinationNode->addLeaf(temp);
      found = true;
    }
  }
  if (!found) {
    qCritical("%s not found!", sourceName.toUtf8().constData());
  }
}

void dumpTree(DirectoryTree::Node *node, int indent)
{
  for (DirectoryTree::const_leaf_reverse_iterator iter = node->leafsRBegin();
       iter != node->leafsREnd(); ++iter) {
    qDebug("%.*s%s", indent, " ", iter->getName().toUtf8().constData());
  }

  for (DirectoryTree::const_node_iterator iter = node->nodesBegin(); iter != node->nodesEnd(); ++iter) {
    qDebug("%.*s-- %s", indent, " ", (*iter)->getData().name.toUtf8().constData());
    dumpTree(*iter, indent + 2);
  }
}


bool FomodInstallerDialog::copyFileIterator(DirectoryTree *sourceTree, DirectoryTree *destinationTree, const FileDescriptor *descriptor)
{
  QString source = (m_FomodPath.length() != 0) ? (m_FomodPath + "\\" + descriptor->m_Source)
                                               : descriptor->m_Source;
  QString destination = descriptor->m_Destination;
  try {
    if (descriptor->m_IsFolder) {
      DirectoryTree::Node *sourceNode = findNode(sourceTree, source, false);
      DirectoryTree::Node *targetNode = findNode(destinationTree, destination, true);
      moveTree(targetNode, sourceNode);
    } else {
      copyLeaf(sourceTree, source, destinationTree, destination);
    }
    return true;
  } catch (const MyException &e) {
    qCritical("failed to extract %s to %s: %s",
              source.toUtf8().constData(), destination.toUtf8().constData(), e.what());
    return false;
  }
}


bool FomodInstallerDialog::testCondition(int maxIndex, const ValueCondition *valCondition) const
{
  return testCondition(maxIndex, valCondition->m_Name, valCondition->m_Value);
}


bool FomodInstallerDialog::testCondition(int maxIndex, const SubCondition *condition) const
{
  bool match = condition->m_Operator == OP_AND;
  for (auto conditionIter = condition->m_Conditions.begin();
       conditionIter != condition->m_Conditions.end(); ++conditionIter) {
    bool conditionMatches = (*conditionIter)->test(maxIndex, this);
    if (conditionMatches && (condition->m_Operator == OP_OR)) {
      match = true;
      break;
    } else if (!conditionMatches && (condition->m_Operator == OP_AND)) {
      match = false;
      break;
    }
  }
  return match;
}


QString FomodInstallerDialog::toString(IPluginList::PluginState state)
{
  switch (state) {
    case IPluginList::STATE_MISSING: return "Missing";
    case IPluginList::STATE_INACTIVE: return "Inactive";
    case IPluginList::STATE_ACTIVE: return "Active";
  }
  throw MyException(tr("invalid plugin state %1").arg(state));
}


bool FomodInstallerDialog::testCondition(int, const FileCondition *condition) const
{
  return toString(m_FileCheck(condition->m_File)) == condition->m_State;
}



//#error "incomplete support for nested conditions. heap-allocated conditions aren't cleaned up yet"

DirectoryTree *FomodInstallerDialog::updateTree(DirectoryTree *tree)
{
  std::vector<FileDescriptor*> descriptorList;

  // enable all required files
  for (std::vector<FileDescriptor*>::iterator iter = m_RequiredFiles.begin(); iter != m_RequiredFiles.end(); ++iter) {
    descriptorList.push_back(*iter);
  }

  // enable all conditional file installs (files programatically selected by conditions instead of a user selection. usually dependencies)
  for (std::vector<ConditionalInstall>::iterator installIter = m_ConditionalInstalls.begin();
       installIter != m_ConditionalInstalls.end(); ++installIter) {
    SubCondition *condition = &installIter->m_Condition;
    if (condition->test(ui->stepsStack->count(), this)) {
      for (std::vector<FileDescriptor*>::iterator fileIter = installIter->m_Files.begin();
           fileIter != installIter->m_Files.end(); ++fileIter) {
        descriptorList.push_back(*fileIter);
      }
    }
  }

  // enable all user-enabled choices
  for (int i = 0; i < ui->stepsStack->count(); ++i) {
    if (testVisible(i)) {
      QList<QAbstractButton*> choices = ui->stepsStack->widget(i)->findChildren<QAbstractButton*>("choice");
      foreach (QAbstractButton* choice, choices) {
        if (choice->isChecked()) {
          QVariantList fileList = choice->property("files").toList();
          foreach (QVariant fileVariant, fileList) {
            descriptorList.push_back(fileVariant.value<FileDescriptor*>());
          }
        }
      }
    }
  }

//  dumpTree(newTree, 0);

  std::sort(descriptorList.begin(), descriptorList.end(), [] (FileDescriptor *lhs, FileDescriptor *rhs) -> bool {
        return lhs->m_Priority < rhs->m_Priority;
      });

  DirectoryTree *newTree = new DirectoryTree;

  foreach (const FileDescriptor *file, descriptorList) {
    copyFileIterator(tree, newTree, file);
  }

  return newTree;
}


void FomodInstallerDialog::highlightControl(QAbstractButton *button)
{
  QVariant screenshotName = button->property("screenshot");
  if (screenshotName.isValid()) {
    QString screenshotFileName = screenshotName.toString();
    if (!screenshotFileName.isEmpty()) {
      QString temp = QDir::tempPath() + "/" + m_FomodPath + "/" + QDir::fromNativeSeparators(screenshotFileName);
      QImage screenshot(temp);
      if (screenshot.isNull()) {
        qWarning(">%s< is a null image", qPrintable(temp));
      } else {
        QPixmap tempPix = QPixmap::fromImage(screenshot);
        ui->screenshotLabel->setScalablePixmap(tempPix);
      }
    } else {
      ui->screenshotLabel->setPixmap(QPixmap());
    }
  }
  ui->descriptionText->setText(button->property("description").toString());
}


bool FomodInstallerDialog::eventFilter(QObject *object, QEvent *event)
{
  QAbstractButton *button = qobject_cast<QAbstractButton*>(object);
  if ((button != NULL) && (event->type() == QEvent::HoverEnter)) {
    highlightControl(button);

  }
  return QDialog::eventFilter(object, event);
}


QString FomodInstallerDialog::readContent(QXmlStreamReader &reader)
{
  if (reader.readNext() == QXmlStreamReader::Characters) {
    return reader.text().toString();
  } else {
    return QString();
  }
}


QString FomodInstallerDialog::readContentUntil(QXmlStreamReader &reader, const QString &endTag)
{
  QString result;
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name().compare(endTag) == 0))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::Characters) {
      result += reader.text().toString();
    }
  }
  return result;
}


void FomodInstallerDialog::parseInfo(QXmlStreamReader &reader)
{
  while (!reader.atEnd() && !reader.hasError()) {
    switch (reader.readNext()) {
      case QXmlStreamReader::StartElement: {
        if (reader.name() == "Name") {
          m_ModName.update(readContent(reader), GUESS_META);
          updateNameEdit();
        } else if (reader.name() == "Author") {
          ui->authorLabel->setText(readContent(reader));
        } else if (reader.name() == "Version") {
          ui->versionLabel->setText(readContent(reader));
        } else if (reader.name() == "Id") {
          m_ModID = readContent(reader).toInt();
        } else if (reader.name() == "Website") {
          QString url = readContent(reader);
          ui->websiteLabel->setText(tr("<a href=\"%1\">Link</a>").arg(url));
          ui->websiteLabel->setToolTip(url);
        }
      } break;
      default: {} break;
    }
  }
  if (reader.hasError()) {
    throw XmlParseError(QString("%1 in line %2").arg(reader.errorString()).arg(reader.lineNumber()));
  }
}


FomodInstallerDialog::ItemOrder FomodInstallerDialog::getItemOrder(const QString &orderString)
{
  if (orderString == "Ascending") {
    return ORDER_ASCENDING;
  } else if (orderString == "Descending") {
    return ORDER_DESCENDING;
  } else if (orderString == "Explicit") {
    return ORDER_EXPLICIT;
  } else {
    throw MyException(tr("unsupported order type %1").arg(orderString));
  }
}


FomodInstallerDialog::GroupType FomodInstallerDialog::getGroupType(const QString &typeString)
{
  if (typeString == "SelectAtLeastOne") {
    return TYPE_SELECTATLEASTONE;
  } else if (typeString == "SelectAtMostOne") {
    return TYPE_SELECTATMOSTONE;
  } else if (typeString == "SelectExactlyOne") {
    return TYPE_SELECTEXACTLYONE;
  } else if (typeString == "SelectAny") {
    return TYPE_SELECTANY;
  } else if (typeString == "SelectAll") {
    return TYPE_SELECTALL;
  } else {
    throw MyException(tr("unsupported group type %1").arg(typeString));
  }
}


FomodInstallerDialog::PluginType FomodInstallerDialog::getPluginType(const QString &typeString)
{
  if (typeString == "Required") {
    return FomodInstallerDialog::TYPE_REQUIRED;
  } else if (typeString == "Optional") {
    return FomodInstallerDialog::TYPE_OPTIONAL;
  } else if (typeString == "Recommended") {
    return FomodInstallerDialog::TYPE_RECOMMENDED;
  } else if (typeString == "NotUsable") {
    return FomodInstallerDialog::TYPE_NOTUSABLE;
  } else if (typeString == "CouldBeUsable") {
    return FomodInstallerDialog::TYPE_COULDBEUSABLE;
  } else {
    qCritical("invalid plugin type %s", typeString.toUtf8().constData());
    return FomodInstallerDialog::TYPE_OPTIONAL;
  }
}


void FomodInstallerDialog::readFileList(QXmlStreamReader &reader, std::vector<FileDescriptor*> &fileList)
{
  QStringRef openTag = reader.name();
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == openTag))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if ((reader.name() == "folder") ||
          (reader.name() == "file")) {
        QXmlStreamAttributes attributes = reader.attributes();
        FileDescriptor *file = new FileDescriptor(this);
        file->m_Source = attributes.value("source").toString();
        file->m_Destination = attributes.hasAttribute("destination") ? attributes.value("destination").toString()
                                                                     : file->m_Source;
        file->m_Priority = attributes.hasAttribute("priority") ? attributes.value("priority").toString().toInt()
                                                               : 0;
        file->m_IsFolder = reader.name() == "folder";
        file->m_InstallIfUsable = attributes.hasAttribute("installIfUsable") ? (attributes.value("installIfUsable").compare("true") == 0)
                                                                             : false;
        file->m_AlwaysInstall = attributes.hasAttribute("alwaysInstall") ? (attributes.value("alwaysInstall").compare("true") == 0)
                                                                         : false;

        fileList.push_back(file);
      }
    }
  }
}


void FomodInstallerDialog::readPluginType(QXmlStreamReader &reader, Plugin &plugin)
{
  plugin.m_Type = TYPE_OPTIONAL;
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "typeDescriptor"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "type") {
        plugin.m_Type = getPluginType(reader.attributes().value("name").toString());
      }
    }
  }
}


void FomodInstallerDialog::readConditionFlags(QXmlStreamReader &reader, Plugin &plugin)
{
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "conditionFlags"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "flag") {
        QStringRef var = reader.attributes().value("name");
        QString name = var.toString();
        QString content = readContent(reader);
        plugin.m_Condition.m_Conditions.push_back(new ValueCondition(name, content));
      }
    }
  }
}


bool FomodInstallerDialog::byPriority(const FileDescriptor *LHS, const FileDescriptor *RHS)
{
  return LHS->m_Priority < RHS->m_Priority;
}


FomodInstallerDialog::Plugin FomodInstallerDialog::readPlugin(QXmlStreamReader &reader)
{
  Plugin result;
  result.m_Name = reader.attributes().value("name").toString();

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "plugin"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "description") {
        result.m_Description = readContentUntil(reader, "description").trimmed();
      } else if (reader.name() == "image") {
        result.m_ImagePath = reader.attributes().value("path").toString();
      } else if (reader.name() == "typeDescriptor") {
        readPluginType(reader, result);
      } else if (reader.name() == "conditionFlags") {
        readConditionFlags(reader, result);
      } else if (reader.name() == "files") {
        readFileList(reader, result.m_Files);
      }
    }
  }

  std::sort(result.m_Files.begin(), result.m_Files.end(), byPriority);

  return result;
}


void FomodInstallerDialog::readPlugins(QXmlStreamReader &reader, GroupType groupType, QLayout *layout)
{
  ItemOrder pluginOrder = reader.attributes().hasAttribute("order") ? getItemOrder(reader.attributes().value("order").toString())
                                                                    : ORDER_ASCENDING;
  bool first = true;
  bool maySelectMore = true;

  std::vector<QAbstractButton*> controls;

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "plugins")) &&
         !reader.atEnd()) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "plugin") {
        Plugin plugin = readPlugin(reader);
        QAbstractButton *newControl = NULL;
        switch (groupType) {
          case TYPE_SELECTATLEASTONE:
          case TYPE_SELECTANY: {
            newControl = new QCheckBox(plugin.m_Name);
          } break;
          case TYPE_SELECTATMOSTONE: {
            newControl = new QRadioButton(plugin.m_Name);
          } break;
          case TYPE_SELECTEXACTLYONE: {
            newControl = new QRadioButton(plugin.m_Name);
            if (first) {
              newControl->setChecked(true);
            }
          } break;
          case TYPE_SELECTALL: {
            newControl = new QCheckBox(plugin.m_Name);
            newControl->setChecked(true);
            newControl->setEnabled(false);
          } break;
        }
        newControl->setObjectName("choice");
        newControl->setAttribute(Qt::WA_Hover);

        switch (plugin.m_Type) {
          case TYPE_REQUIRED: {
            newControl->setChecked(true);
            newControl->setEnabled(false);
            newControl->setToolTip(tr("This component is required"));
          } break;
          case TYPE_RECOMMENDED: {
            if (maySelectMore) {
              newControl->setChecked(true);
            }
            newControl->setToolTip(tr("It is recommended you enable this component"));
            if ((groupType == TYPE_SELECTATMOSTONE) || (groupType == TYPE_SELECTEXACTLYONE)) {
              maySelectMore = false;
            }
          } break;
          case TYPE_OPTIONAL: {
            newControl->setToolTip(tr("Optional component"));
          } break;
          case TYPE_NOTUSABLE: {
            newControl->setChecked(false);
            newControl->setEnabled(false);
            newControl->setToolTip(tr("This component is not usable in combination with other installed plugins"));
          } break;
          case TYPE_COULDBEUSABLE: {
            newControl->setCheckable(true);
            newControl->setIcon(QIcon(":/new/guiresources/warning_16"));
            newControl->setToolTip(tr("You may be experiencing instability in combination with other installed plugins"));
          } break;
        }

        newControl->setProperty("plugintype", plugin.m_Type);
        newControl->setProperty("screenshot", plugin.m_ImagePath);
        newControl->setProperty("description", plugin.m_Description);
        QVariantList fileList;
        for (std::vector<FileDescriptor*>::iterator iter = plugin.m_Files.begin(); iter != plugin.m_Files.end(); ++iter) {
          fileList.append(qVariantFromValue(*iter));
        }
        newControl->setProperty("files", fileList);
        QVariantList conditionFlags;
        for (std::vector<Condition*>::const_iterator iter = plugin.m_Condition.m_Conditions.begin();
             iter != plugin.m_Condition.m_Conditions.end(); ++iter) {
          ValueCondition *condition = dynamic_cast<ValueCondition*>(*iter);
          if ((condition != NULL) && (condition->m_Name.length() != 0)) {
            conditionFlags.append(qVariantFromValue(ValueCondition(condition->m_Name, condition->m_Value)));
          }
        }
        newControl->setProperty("conditionFlags", conditionFlags);
        newControl->installEventFilter(this);
        controls.push_back(newControl);
        first = false;
      }
    }
  }

  if (pluginOrder == ORDER_ASCENDING) {
    std::sort(controls.begin(), controls.end(), ControlsAscending);
  } else if (pluginOrder == ORDER_DESCENDING) {
    std::sort(controls.begin(), controls.end(), ControlsDescending);
  }

  for (std::vector<QAbstractButton*>::const_iterator iter = controls.begin(); iter != controls.end(); ++iter) {
    layout->addWidget(*iter);
  }

  if (groupType == TYPE_SELECTATMOSTONE) {
    QRadioButton *newButton = new QRadioButton(tr("None"));
    if (maySelectMore) {
      newButton->setChecked(true);
    }
    layout->addWidget(newButton);
  }
}


void FomodInstallerDialog::readGroup(QXmlStreamReader &reader, QLayout *layout)
{
  //FileGroup result;
  QString name = reader.attributes().value("name").toString();
  GroupType type = getGroupType(reader.attributes().value("type").toString());

  if (type == TYPE_SELECTATLEASTONE) {
    QLabel *label = new QLabel(tr("Select one or more of these options:"));
    layout->addWidget(label);
  }

  QGroupBox *groupBox = new QGroupBox(name);

  QVBoxLayout *groupLayout = new QVBoxLayout;

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "group"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "plugins") {
        readPlugins(reader, type, groupLayout);
      }
    }
  }

  groupBox->setLayout(groupLayout);
  layout->addWidget(groupBox);
}


void FomodInstallerDialog::readGroups(QXmlStreamReader &reader, QLayout *layout)
{
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "optionalFileGroups"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "group") {
        readGroup(reader, layout);
      }
    }
  }
}


void FomodInstallerDialog::readVisible(QXmlStreamReader &reader, QVariantList &conditions, ConditionOperator &op)
{
  if (reader.attributes().hasAttribute("operator")) {
    QString opName = reader.attributes().value("operator").toString();
    if (opName == "Or") {
      op = OP_OR;
    } else {
      op = OP_AND;
    }
  } else {
    op = OP_AND;
  }

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "visible"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "flagDependency") {
        ValueCondition condition(reader.attributes().value("flag").toString(),
                                 reader.attributes().value("value").toString());
        conditions.append(qVariantFromValue(condition));
      }
    }
  }
}

QGroupBox *FomodInstallerDialog::readInstallerStep(QXmlStreamReader &reader)
{
  QString name = reader.attributes().value("name").toString();
  QGroupBox *page = new QGroupBox(name);
  QVBoxLayout *pageLayout = new QVBoxLayout;
  QScrollArea *scrollArea = new QScrollArea;
  QFrame *scrolledArea = new QFrame;
  QVBoxLayout *scrollLayout = new QVBoxLayout;

  QVariantList conditions;
  ConditionOperator conditionOperator;

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "installStep"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "optionalFileGroups") {
        readGroups(reader, scrollLayout);
      } else if (reader.name() == "visible") {
        readVisible(reader, conditions, conditionOperator);
      }
    }
  }
  if (conditions.length() != 0) {
    page->setProperty("conditions", conditions);
    page->setProperty("conditionOperator", conditionOperator);
  }

  scrolledArea->setLayout(scrollLayout);
  scrollArea->setWidget(scrolledArea);
  scrollArea->setWidgetResizable(true);
  pageLayout->addWidget(scrollArea);
  page->setLayout(pageLayout);
  return page;
}


void FomodInstallerDialog::readInstallerSteps(QXmlStreamReader &reader)
{
  ItemOrder stepOrder = reader.attributes().hasAttribute("order") ? getItemOrder(reader.attributes().value("order").toString())
                                                                    : ORDER_ASCENDING;

  std::vector<QGroupBox*> pages;

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "installSteps"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "installStep") {
        pages.push_back(readInstallerStep(reader));
      }
    }
  }

  if (stepOrder == ORDER_ASCENDING) {
    std::sort(pages.begin(), pages.end(), PagesAscending);
  } else if (stepOrder == ORDER_DESCENDING) {
    std::sort(pages.begin(), pages.end(), PagesDescending);
  }

  for (std::vector<QGroupBox*>::const_iterator iter = pages.begin(); iter != pages.end(); ++iter) {
    ui->stepsStack->addWidget(*iter);
  }
}


void FomodInstallerDialog::readConditionalDependency(QXmlStreamReader &reader, SubCondition &conditional)
{
  QStringRef dependencyOperator = reader.attributes().value("operator");
  if (dependencyOperator == "And") {
    conditional.m_Operator = OP_AND;
  } else if (dependencyOperator == "Or") {
    conditional.m_Operator = OP_OR;
  } // otherwise operator is not set (which we can ignore) or invalid (which we should report actually)

  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "dependencies"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "flagDependency") {
        conditional.m_Conditions.push_back(new ValueCondition(reader.attributes().value("flag").toString(),
                                                              reader.attributes().value("value").toString()));
      } else if (reader.name() == "dependencies") {
        SubCondition *nested = new SubCondition();
        readConditionalDependency(reader, *nested);
        conditional.m_Conditions.push_back(nested);
      } else if (reader.name() == "fileDependency") {
        conditional.m_Conditions.push_back(new FileCondition(reader.attributes().value("file").toString(),
                                                             reader.attributes().value("state").toString()));
      }
    }
  }
}


FomodInstallerDialog::ConditionalInstall FomodInstallerDialog::readConditionalPattern(QXmlStreamReader &reader)
{
  ConditionalInstall result;
  result.m_Condition.m_Operator = OP_AND;
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "pattern"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "dependencies") {
        readConditionalDependency(reader, result.m_Condition);
      } else if (reader.name() == "files") {
        readFileList(reader, result.m_Files);
      }
    }
  }
  return result;
}


void FomodInstallerDialog::readConditionalFileInstalls(QXmlStreamReader &reader)
{
  while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
           (reader.name() == "conditionalFileInstalls"))) {
    if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
    if (reader.tokenType() == QXmlStreamReader::StartElement) {
      if (reader.name() == "patterns") {
        while (!((reader.readNext() == QXmlStreamReader::EndElement) &&
                 (reader.name() == "patterns"))) {
          if (reader.tokenType() == QXmlStreamReader::Invalid) throw MyException(tr("Invalid xml token"));
          if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == "pattern") {
              m_ConditionalInstalls.push_back(readConditionalPattern(reader));
            }
          }
        }
      }
    }
  }
}


void FomodInstallerDialog::parseModuleConfig(QXmlStreamReader &reader)
{
  while (!reader.atEnd() && !reader.hasError()) {
    switch (reader.readNext()) {
      case QXmlStreamReader::StartElement: {
        if (reader.name() == "installSteps") {
          readInstallerSteps(reader);
        } else if (reader.name() == "requiredInstallFiles") {
          readFileList(reader, m_RequiredFiles);
        } else if (reader.name() == "conditionalFileInstalls") {
          readConditionalFileInstalls(reader);
        }
      } break;
      default: {} break;
    }
  }
  if (reader.hasError()) {
    throw XmlParseError(QString("%1 in line %2").arg(reader.errorString()).arg(reader.lineNumber()));
  }
  activateCurrentPage();
}


void FomodInstallerDialog::on_manualBtn_clicked()
{
  m_Manual = true;
  this->reject();
}

void FomodInstallerDialog::on_cancelBtn_clicked()
{
  this->reject();
}


void FomodInstallerDialog::on_websiteLabel_linkActivated(const QString &link)
{
  ::ShellExecuteW(NULL, L"open", ToWString(link).c_str(), NULL, NULL, SW_SHOWNORMAL);
}


void FomodInstallerDialog::activateCurrentPage()
{
  QList<QAbstractButton*> choices = ui->stepsStack->currentWidget()->findChildren<QAbstractButton*>("choice");
  if (choices.count() > 0) {
    highlightControl(choices.at(0));
  }
}

bool FomodInstallerDialog::testCondition(int maxIndex, const QString &flag, const QString &value) const
{
  // a caching mechanism for previously calculated condition results. otherwise going through multiple pages can get
  // very slow
  auto iter = m_ConditionCache.find(flag);
  if (iter != m_ConditionCache.end()) {
    return iter->second == value;
  }
  // unset and set conditions are stored separately since the unset conditions need to be flushed when we move to the next page (condition
  // could be set there) while the set conditions need to be flushed when we move back in in the installer
  if (m_ConditionsUnset.find(flag) != m_ConditionsUnset.end()) {
    return value.isEmpty();
  }

  // iterate through all enabled condition flags on all activated controls on all visible pages if one of them matches the condition
  for (int i = 0; i < maxIndex; ++i) {
    if (testVisible(i)) {
      QWidget *page = ui->stepsStack->widget(i);
      QList<QAbstractButton*> choices = page->findChildren<QAbstractButton*>("choice");
      foreach (QAbstractButton* choice, choices) {
        if (choice->isChecked()) {
          QVariant temp = choice->property("conditionFlags");
          if (temp.isValid()) {
            QVariantList conditionFlags = temp.toList();
            for (QVariantList::const_iterator iter = conditionFlags.begin(); iter != conditionFlags.end(); ++iter) {
              ValueCondition condition = iter->value<ValueCondition>();
              m_ConditionCache[condition.m_Name] = condition.m_Value;
              if ((condition.m_Name == flag) && (condition.m_Value == value)) {
                return true;
              }
            }
          }
        }
      }
    }
  }
  m_ConditionsUnset.insert(flag);
  return value.isEmpty();
}


bool FomodInstallerDialog::testVisible(int pageIndex) const
{
  QWidget *page = ui->stepsStack->widget(pageIndex);
  QVariant temp = page->property("conditions");
  int op = page->property("conditionOperator").toInt();
  if (temp.isValid()) {
    // go through the conditions for this page. returns false if one isn't fulfilled, true otherwise
    QVariantList conditions = temp.toList();
    for (QVariantList::const_iterator iter = conditions.begin(); iter != conditions.end(); ++iter) {
      ValueCondition condition = iter->value<ValueCondition>();
      bool res = testCondition(pageIndex, condition.m_Name, condition.m_Value);
      if ((op == OP_AND) && !res) {
        return false;
      } else if ((op == OP_OR) && res) {
        return true;
      }
    }
    // for OP_AND this means no condition failed. for OP_OR it means none matched
    return op == OP_AND;
  } else {
    return true;
  }
}


bool FomodInstallerDialog::nextPage()
{
  m_ConditionsUnset.clear();
  int oldIndex = ui->stepsStack->currentIndex();

  int index = oldIndex + 1;
  // find the next "visible" install step
  while (index < ui->stepsStack->count()) {
    if (testVisible(index)) {
      ui->stepsStack->setCurrentIndex(index);
      ui->stepsStack->currentWidget()->setProperty("previous", oldIndex);
      return true;
    }
    ++index;
  }
  // no more visible pages -> install
  return false;
}


void FomodInstallerDialog::on_nextBtn_clicked()
{
  if (ui->stepsStack->currentIndex() == ui->stepsStack->count() - 1) {
    this->accept();
  } else {
    if (nextPage()) {
      if (ui->stepsStack->currentIndex() == ui->stepsStack->count() - 1) {
        ui->nextBtn->setText(tr("Install"));
      }
      ui->prevBtn->setEnabled(true);
      activateCurrentPage();
    } else {
      this->accept();
    }
  }
}

void FomodInstallerDialog::on_prevBtn_clicked()
{
  if (ui->stepsStack->currentIndex() != 0) {
    int previousIndex = 0;
    QVariant temp = ui->stepsStack->currentWidget()->property("previous");
    if (temp.isValid()) {
      previousIndex = temp.toInt();
    } else {
      previousIndex = ui->stepsStack->currentIndex() - 1;
    }
    ui->stepsStack->setCurrentIndex(previousIndex);
    m_ConditionCache.clear();
    ui->nextBtn->setText(tr("Next"));
  }
  if (ui->stepsStack->currentIndex() == 0) {
    ui->prevBtn->setEnabled(false);
  }
  activateCurrentPage();
}
