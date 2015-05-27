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
#include "xmlreader.h"

#include <scopeguard.h>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QImage>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QTextCodec>
#include <Shellapi.h>
#include <boost/assign.hpp>
#include <sstream>


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
                                           const std::function<MOBase::IPluginList::PluginStates(const QString &)> &fileCheck,
                                           QWidget *parent)
  : QDialog(parent), ui(new Ui::FomodInstallerDialog), m_ModName(modName), m_ModID(-1),
    m_FomodPath(fomodPath), m_Manual(false), m_FileCheck(fileCheck),
    m_CacheConditions(true),
    m_FileSystemItemSequence()
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
          XmlReader reader(codec->fromUnicode(QString("<?xml version=\"1.0\" encoding=\"%1\" ?>").arg(encoding)) + headerlessData);
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
    XmlReader reader(&file);
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
        XmlReader reader(codec->fromUnicode(QString("<?xml version=\"1.0\" encoding=\"%1\" ?>").arg(encoding)) + headerlessData);
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

namespace {

QString getNodePath(DirectoryTree::Node const *node)
{
  {
    QString s;
    if (node->getParent() != nullptr) {
      s = getNodePath(node->getParent());
      if (s != "") {
        s += "/";
      }
    }
    return s + node->getData().name;
  }
}

}

void FomodInstallerDialog::moveTree(DirectoryTree::Node *target, DirectoryTree::Node *source, DirectoryTree::Overwrites *overwrites)
{
  for (DirectoryTree::const_node_iterator iter = source->nodesBegin(); iter != source->nodesEnd();) {
    target->addNode(*iter, true, overwrites);
    iter = source->detach(iter);
  }

  for (DirectoryTree::const_leaf_reverse_iterator iter = source->leafsRBegin();
       iter != source->leafsREnd(); ++iter) {
    target->addLeaf(*iter, true, overwrites);
  }
}


DirectoryTree::Node *FomodInstallerDialog::findNode(DirectoryTree::Node *node, const QString &path, bool create)
{
  if (path.length() == 0) {
    return node;
  }

  int pos = path.indexOf(QRegExp("[\\\\/]"));
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
                                    DirectoryTree::Node *destinationTree, const QString &destinationPath,
                                    DirectoryTree::Overwrites *overwrites,
                                    Leaves *leaves, int pri)
{
  int sourceFileIndex = sourcePath.lastIndexOf('\\');
  if (sourceFileIndex == -1) {
    sourceFileIndex = sourcePath.lastIndexOf('/');
    if (sourceFileIndex == -1) {
      sourceFileIndex = 0;
    }
  }
  DirectoryTree::Node *sourceNode = sourceFileIndex == 0 ? sourceTree : findNode(sourceTree, sourcePath.mid(0, sourceFileIndex), false);
  applyPriority(leaves, sourceNode, pri);

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
      destinationNode->addLeaf(temp, true, overwrites);
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

void FomodInstallerDialog::applyPriority(Leaves *leaves, DirectoryTree::Node *node, int priority)
{
  for (DirectoryTree::leaf_iterator iter = node->leafsBegin(); iter != node->leafsEnd(); ++iter) {
    LeafInfo info = { priority, getNodePath(node) + "/" + iter->getName() };
    leaves->insert(std::make_pair(iter->getIndex(), info));
  }
  for (DirectoryTree::node_iterator iter = node->nodesBegin(); iter != node->nodesEnd(); ++iter) {
    applyPriority(leaves, *iter, priority);
  }
}

bool FomodInstallerDialog::copyFileIterator(DirectoryTree *sourceTree, DirectoryTree *destinationTree,
                                            const FileDescriptor *descriptor,
                                            Leaves *leaves, DirectoryTree::Overwrites *overwrites)
{
  QString source = (m_FomodPath.length() != 0) ? (m_FomodPath + "/" + descriptor->m_Source)
                                               : descriptor->m_Source;
  int pri = descriptor->m_Priority;
  QString destination = descriptor->m_Destination;
  try {
    if (descriptor->m_IsFolder) {
      DirectoryTree::Node *sourceNode = findNode(sourceTree, source, false);
      //Now apply the priority to the sourceNode tree
      applyPriority(leaves, sourceNode, pri);
      DirectoryTree::Node *targetNode = findNode(destinationTree, destination, true);
      moveTree(targetNode, sourceNode, overwrites);
    } else {
      copyLeaf(sourceTree, source, destinationTree, destination, overwrites, leaves, pri);
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

bool FomodInstallerDialog::testCondition(int maxIndex, const ConditionFlag *conditionFlag) const
{
  return testCondition(maxIndex, conditionFlag->m_Name, conditionFlag->m_Value);
}

bool FomodInstallerDialog::testCondition(int maxIndex, const SubCondition *condition) const
{
  ConditionOperator op = condition->m_Operator;
  for (Condition const *cond : condition->m_Conditions) {
    bool conditionMatches = cond->test(maxIndex, this);
    if (op == OP_OR && conditionMatches) {
      return true;
    }
    if (op == OP_AND && !conditionMatches) {
      return false;
    }
  }
  //If we get through here, everything matched (AND) or nothing matched (OR)
  return op == OP_AND;
}

QString FomodInstallerDialog::toString(IPluginList::PluginStates state)
{
  if (state.testFlag(IPluginList::STATE_MISSING)) return "Missing";
  if (state.testFlag(IPluginList::STATE_INACTIVE)) return "Inactive";
  if (state.testFlag(IPluginList::STATE_ACTIVE)) return "Active";
  throw MyException(tr("invalid plugin state"));
}

bool FomodInstallerDialog::testCondition(int, const FileCondition *condition) const
{
  return toString(m_FileCheck(condition->m_File)) == condition->m_State;
}

DirectoryTree *FomodInstallerDialog::updateTree(DirectoryTree *tree)
{
  FileDescriptorList descriptorList;

  // enable all required files
  for (FileDescriptorList::iterator iter = m_RequiredFiles.begin(); iter != m_RequiredFiles.end(); ++iter) {
    descriptorList.push_back(*iter);
  }

  // enable all conditional file installs (files programatically selected by conditions instead of a user selection. usually dependencies)
  for (std::vector<ConditionalInstall>::iterator installIter = m_ConditionalInstalls.begin();
       installIter != m_ConditionalInstalls.end(); ++installIter) {
    SubCondition *condition = &installIter->m_Condition;
    if (condition->test(ui->stepsStack->count(), this)) {
      for (FileDescriptorList::iterator fileIter = installIter->m_Files.begin();
           fileIter != installIter->m_Files.end(); ++fileIter) {
        descriptorList.push_back(*fileIter);
      }
    }
  }
/**/qDebug() << "Count is " <<  ui->stepsStack->count() << " vs " << m_PageVisible.size();
  /**/std::ostringstream s;
  for (std::size_t i = 0; i < m_PageVisible.size(); ++i) {
    if (m_PageVisible[i]) s<< " " << i;
  }
  qDebug() << s.str().c_str();

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

  std::sort(descriptorList.begin(), descriptorList.end(), byPriority);

  DirectoryTree *newTree = new DirectoryTree;
  Leaves leaves;
  DirectoryTree::Overwrites overwrites;

  foreach (const FileDescriptor *file, descriptorList) {
    copyFileIterator(tree, newTree, file, &leaves, &overwrites);
  }

  for (auto overwrite : overwrites) {
    if (leaves[overwrite.first].priority == leaves[overwrite.second].priority) {
      qWarning() << "Overriding " << leaves[overwrite.first].path << " with " <<
                    leaves[overwrite.second].path << " which has the same priority";
    }
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
  if ((button != nullptr) && (event->type() == QEvent::HoverEnter)) {
    highlightControl(button);
  }
  return QDialog::eventFilter(object, event);
}


QString FomodInstallerDialog::readContent(QXmlStreamReader &reader)
{
  if (reader.readNext() == XmlReader::Characters) {
    return reader.text().toString();
  } else {
    return QString();
  }
}


void FomodInstallerDialog::parseInfo(QXmlStreamReader &reader)
{
  while (!reader.atEnd()) {
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


void FomodInstallerDialog::readFileList(XmlReader &reader, FileDescriptorList &fileList)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "folder" || reader.name() == "file") {
      QXmlStreamAttributes attributes = reader.attributes();
      //This is a horrendous hack. It doesn't make sense to specify an empty source folder name,
      //as it would require you to copy everything including the fomod directory. However, people
      //have been known to write entries like <folder source="" destination=""/> in order to
      //achieve an option that does nothing. Are groups and buttons that hard?
      //An empty source file is very probably a serious error but given people do the above, I'm
      //assuming that they probably assume <file source="" destination=""/> will work the same,
      //so I'm not differentiating.
      //Similarly, I'm not checking for the destination if the source is blank. Why'd you want to
      //copy the fomod directory on an install?
      if (attributes.value("source").isEmpty()) {
        qDebug("Ignoring %s entry with empty source.", reader.name().toUtf8().constData());
      } else {
        FileDescriptor *file = new FileDescriptor(this);
        file->m_Source = attributes.value("source").toString();
        file->m_Destination = attributes.hasAttribute("destination") ? attributes.value("destination").toString()
                                                                     : file->m_Source;
        file->m_Priority = attributes.hasAttribute("priority") ? attributes.value("priority").toString().toInt()
                                                               : 0;
        file->m_FileSystemItemSequence = ++m_FileSystemItemSequence;
        file->m_IsFolder = reader.name() == "folder";
        file->m_InstallIfUsable = attributes.hasAttribute("installIfUsable") ? (attributes.value("installIfUsable").compare("true") == 0)
                                                                             : false;
        file->m_AlwaysInstall = attributes.hasAttribute("alwaysInstall") ? (attributes.value("alwaysInstall").compare("true") == 0)
                                                                         : false;

        fileList.push_back(file);
      }
      reader.finishedElement();
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readDependencyPattern(XmlReader &reader, DependencyPattern &pattern)
{
  //sequence
  //  dependency
  //  type
  QString self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "dependencies") {
      readCompositeDependency(reader, pattern.condition);
    } else if (reader.name() == "type") {
      pattern.type = getPluginType(reader.attributes().value("name").toString());
      reader.finishedElement();
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readDependencyPatternList(XmlReader &reader, DependencyPatternList &patterns)
{
  QString self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "pattern") {
      DependencyPattern pattern;
      readDependencyPattern(reader, pattern);
      patterns.push_back(pattern);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readDependencyPluginType(XmlReader &reader, PluginTypeInfo &info)
{
  //sequence
  // defaultType
  // patterns
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "defaultType") {
      info.m_DefaultType = getPluginType(reader.attributes().value("name").toString());
      reader.finishedElement();
    } else if (reader.name() == "patterns") {
      readDependencyPatternList(reader, info.m_DependencyPatterns);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readPluginType(XmlReader &reader, Plugin &plugin)
{
  //Have a choice here of precisely one of 'type' or 'dependencytype', so this is
  //not strictly necessary
  plugin.m_PluginType.m_DefaultType = TYPE_OPTIONAL;
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "type") {
      plugin.m_PluginType.m_DefaultType = getPluginType(reader.attributes().value("name").toString());
      reader.finishedElement();
    } else if (reader.name() == "dependencyType") {
      readDependencyPluginType(reader, plugin.m_PluginType);
    } else {
      reader.unexpected();
    }
  }
}


void FomodInstallerDialog::readConditionFlagList(XmlReader &reader, ConditionFlagList &condflags)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "flag") {
      QString name = reader.attributes().value("name").toString();
      QString content = reader.getText();
      condflags.push_back(ConditionFlag(name, content));
    } else {
      reader.unexpected();
    }
  }
}


bool FomodInstallerDialog::byPriority(const FileDescriptor *LHS, const FileDescriptor *RHS)
{
  return LHS->m_Priority == RHS->m_Priority ?
                LHS->m_FileSystemItemSequence < RHS->m_FileSystemItemSequence :
                LHS->m_Priority < RHS->m_Priority;
}


FomodInstallerDialog::Plugin FomodInstallerDialog::readPlugin(XmlReader &reader)
{
  Plugin result;
  result.m_Name = reader.attributes().value("name").toString();

  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "description") {
      result.m_Description = reader.getText().trimmed();
    } else if (reader.name() == "image") {
      result.m_ImagePath = reader.attributes().value("path").toString();
      reader.finishedElement();
    } else if (reader.name() == "files") {
      readFileList(reader, result.m_Files);
    } else if (reader.name() == "conditionFlags") {
      readConditionFlagList(reader, result.m_ConditionFlags);
    } else if (reader.name() == "typeDescriptor") {
      readPluginType(reader, result);
    } else {
      reader.unexpected();
    }
  }

  //I (TRT) am not quite sure why this sort is done here. It is done again
  //when the files have been selected before installing them, which seems
  //a more appropriate place.
  std::sort(result.m_Files.begin(), result.m_Files.end(), byPriority);

  return result;
}


FomodInstallerDialog::PluginType FomodInstallerDialog::getPluginDependencyType(const PluginTypeInfo &info) const
{
  if (info.m_DependencyPatterns.size() != 0) {
    //FIXME: I probably shouldn't do this till I display the screen, as this
    //could have value conditions and the value might be set/changed as we go
    //through screens.
    //Hacking around for now
    for (DependencyPattern const &pattern : info.m_DependencyPatterns) {
      if (testCondition(/*maxindex*/1, &pattern.condition)) {
          return pattern.type;
      }
    }
  }
  return info.m_DefaultType;
}

void FomodInstallerDialog::readPluginList(XmlReader &reader, GroupType groupType, QLayout *layout)
{
  ItemOrder pluginOrder = reader.attributes().hasAttribute("order") ? getItemOrder(reader.attributes().value("order").toString())
                                                                    : ORDER_ASCENDING;
  bool maySelectMore = true;
  bool mustSelectOne = groupType == TYPE_SELECTATMOSTONE || groupType == TYPE_SELECTEXACTLYONE;

  std::vector<QAbstractButton*> controls;

  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "plugin") {
      Plugin plugin = readPlugin(reader);
      QAbstractButton *newControl = nullptr;
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
        } break;
        case TYPE_SELECTALL: {
          newControl = new QCheckBox(plugin.m_Name);
          newControl->setChecked(true);
          newControl->setEnabled(false);
        } break;
      }
      newControl->setObjectName("choice");
      newControl->setAttribute(Qt::WA_Hover);

      PluginType type = getPluginDependencyType(plugin.m_PluginType);
      switch (type) {
        case TYPE_REQUIRED: {
          maySelectMore = false;
          newControl->setChecked(true);
          newControl->setEnabled(false);
          newControl->setToolTip(tr("This component is required"));
        } break;
        case TYPE_RECOMMENDED: {
          if (!mustSelectOne || maySelectMore) {
            newControl->setChecked(true);
            maySelectMore = false;
          }
          newControl->setToolTip(tr("It is recommended you enable this component"));
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

      newControl->setProperty("plugintype", type);
      newControl->setProperty("screenshot", plugin.m_ImagePath);
      newControl->setProperty("description", plugin.m_Description);
      QVariantList fileList;
      for (FileDescriptorList::iterator iter = plugin.m_Files.begin(); iter != plugin.m_Files.end(); ++iter) {
        fileList.append(qVariantFromValue(*iter));
      }
      newControl->setProperty("files", fileList);
      QVariantList conditionFlags;
      for (ConditionFlag const &conditionFlag : plugin.m_ConditionFlags) {
        if (! conditionFlag.m_Name.isEmpty()) {
          conditionFlags.append(qVariantFromValue(conditionFlag));
        }
      }
      newControl->setProperty("conditionFlags", conditionFlags);
      newControl->installEventFilter(this);
      //We need somehow to check the 'toggled' signal. how do I do that
      //void QAbstractButton::clicked ( bool checked ) [signal]
      connect(newControl, SIGNAL(clicked()), this, SLOT(widgetButtonClicked()));
      controls.push_back(newControl);
    } else {
      reader.unexpected();
    }
  }

  if (pluginOrder == ORDER_ASCENDING) {
    std::sort(controls.begin(), controls.end(), ControlsAscending);
  } else if (pluginOrder == ORDER_DESCENDING) {
    std::sort(controls.begin(), controls.end(), ControlsDescending);
  }

  if (groupType == TYPE_SELECTEXACTLYONE && maySelectMore) {
    controls[0]->setChecked(true);
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


void FomodInstallerDialog::readGroup(XmlReader &reader, QLayout *layout)
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

  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "plugins") {
      readPluginList(reader, type, groupLayout);
    } else {
      reader.unexpected();
    }
  }

  groupBox->setLayout(groupLayout);
  layout->addWidget(groupBox);
}


void FomodInstallerDialog::readGroupList(XmlReader &reader, QLayout *layout)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "group") {
      readGroup(reader, layout);
    } else {
      reader.unexpected();
    }
  }
}

QGroupBox *FomodInstallerDialog::readInstallStep(XmlReader &reader)
{
  QString name = reader.attributes().value("name").toString();
  QGroupBox *page = new QGroupBox(name);
  QVBoxLayout *pageLayout = new QVBoxLayout;
  QScrollArea *scrollArea = new QScrollArea;
  QFrame *scrolledArea = new QFrame;
  QVBoxLayout *scrollLayout = new QVBoxLayout;

  SubCondition subcondition;

  //sequence:
  //  visible (optional)
  //  optionalFileGroups
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "visible") {
      readCompositeDependency(reader, subcondition);
    } else if (reader.name() == "optionalFileGroups") {
      readGroupList(reader, scrollLayout);
    } else {
      reader.unexpected();
    }
  }

  if (subcondition.m_Conditions.size() != 0) {
    //FIXME Is this actually OK? I'm storing a pointer in the property?
    //Also AFAICS this is subject to memory leaks
    page->setProperty("conditional", qVariantFromValue(subcondition));
  }

  scrolledArea->setLayout(scrollLayout);
  scrollArea->setWidget(scrolledArea);
  scrollArea->setWidgetResizable(true);
  pageLayout->addWidget(scrollArea);
  page->setLayout(pageLayout);
  return page;
}


void FomodInstallerDialog::readStepList(XmlReader &reader)
{
  ItemOrder stepOrder = reader.attributes().hasAttribute("order") ? getItemOrder(reader.attributes().value("order").toString())
                                                                    : ORDER_ASCENDING;

  std::vector<QGroupBox*> pages;

  //sequence installStep (1 or more)
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "installStep") {
      pages.push_back(readInstallStep(reader));
    } else {
      reader.unexpected();
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


void FomodInstallerDialog::readCompositeDependency(XmlReader &reader, SubCondition &conditional)
{
  conditional.m_Operator = OP_AND;
  QStringRef dependencyOperator = reader.attributes().value("operator");
  if (dependencyOperator == "And") {
    conditional.m_Operator = OP_AND;
  } else if (dependencyOperator == "Or") {
    conditional.m_Operator = OP_OR;
  } // otherwise operator is not set (which we can ignore) or invalid (which we should report actually)

  QString const self = reader.name().toString();
  while (reader.getNextElement(self)) {
    if (reader.name() == "fileDependency") {
      conditional.m_Conditions.push_back(new FileCondition(reader.attributes().value("file").toString(),
                                                           reader.attributes().value("state").toString()));
      reader.finishedElement();
    } else if (reader.name() == "flagDependency") {
      conditional.m_Conditions.push_back(new ValueCondition(reader.attributes().value("flag").toString(),
                                                            reader.attributes().value("value").toString()));
      reader.finishedElement();
    // else if gameDependency
    // else if fommDependency
    } else if (reader.name() == "dependencies") {
      SubCondition *nested = new SubCondition();
      readCompositeDependency(reader, *nested);
      conditional.m_Conditions.push_back(nested);
    } else {
      reader.unexpected();
    }
  }
}


FomodInstallerDialog::ConditionalInstall FomodInstallerDialog::readConditionalInstallPattern(XmlReader &reader)
{
  ConditionalInstall result;
  result.m_Condition.m_Operator = OP_AND;
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "dependencies") {
      readCompositeDependency(reader, result.m_Condition);
    } else if (reader.name() == "files") {
      readFileList(reader, result.m_Files);
    } else {
      reader.unexpected();
    }
  }
  return result;
}

void FomodInstallerDialog::readConditionalFilePatternList(XmlReader &reader)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "pattern") {
      m_ConditionalInstalls.push_back(readConditionalInstallPattern(reader));
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readConditionalFileInstallList(XmlReader &reader)
{
  QString const self(reader.name().toString());
  //Technically there should be only one but it's easier to write like this
  while (reader.getNextElement(self)) {
    if (reader.name() == "patterns") {
      readConditionalFilePatternList(reader);
    } else {
      reader.unexpected();
    }
  }
}


void FomodInstallerDialog::readModuleConfiguration(XmlReader &reader)
{
  //sequence:
  //  modulename
  //  optional - moduleImage
  //  optional - moduleDependencies
  //  optional - requiredInstallFiles
  //  optional - installSteps
  //  optional - conditionalFileInstalls
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name() == "moduleName") {
      QString title = reader.getText();
      qDebug() << "module name : "  << title;
    } else if (reader.name() == "moduleImage") {
      //do something useful with the attributes of this
      reader.finishedElement();
    } else if (reader.name() == "moduleDependencies") {
      QString s = reader.readElementText(XmlReader::IncludeChildElements);
      qDebug() << " module dependencies " << s;
      //do something useful with the condition dependencies
      //readCompositeDependency
    } else if (reader.name() == "requiredInstallFiles") {
      readFileList(reader, m_RequiredFiles);
    } else if (reader.name() == "installSteps") {
      readStepList(reader);
    } else if (reader.name() == "conditionalFileInstalls") {
      readConditionalFileInstallList(reader);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::parseModuleConfig(XmlReader &reader)
{
  if (reader.readNext() != XmlReader::StartDocument) {
    throw XmlParseError(QString("Expected document start at line %1").arg(reader.lineNumber()));
  }
  processXmlTag(reader, "config", &FomodInstallerDialog::readModuleConfiguration);
  if (reader.readNext() != XmlReader::EndDocument) {
    throw XmlParseError(QString("Expected document end at line %1").arg(reader.lineNumber()));
  }
  if (reader.hasError()) {
    throw XmlParseError(QString("%1 in line %2").arg(reader.errorString()).arg(reader.lineNumber()));
  }
  //FIXME It might be possible for the first page to be inactive?
  activateCurrentPage();
}


void FomodInstallerDialog::processXmlTag(XmlReader &reader, char const *tag, TagProcessor func)
{
  if (reader.readNext() == XmlReader::StartElement && reader.name() == tag) {
    (this->*func)(reader);
  } else if (! reader.hasError()) {
    reader.raiseError(QString("Expected %1, got %2").arg(tag).arg(reader.name().toString()));
  }
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
  ::ShellExecuteW(nullptr, L"open", ToWString(link).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}


void FomodInstallerDialog::activateCurrentPage()
{
  QList<QAbstractButton*> choices = ui->stepsStack->currentWidget()->findChildren<QAbstractButton*>("choice");
  if (choices.count() > 0) {
    highlightControl(choices.at(0));
  }
  m_PageVisible.push_back(true);
  /**/std::ostringstream s;
  for (std::size_t i = 0; i < m_PageVisible.size(); ++i) {
    if (m_PageVisible[i]) s<< " " << i;
  }
  qDebug() << "Pages visible: " << s.str().c_str();
  updateNextbtnText();
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
            for (QVariant const &variant : conditionFlags) {
              ConditionFlag condition = variant.value<ConditionFlag>();
              if (m_CacheConditions) {
                m_ConditionCache[condition.m_Name] = condition.m_Value;
              }
              if (condition.m_Name == flag) {
                return condition.m_Value == value;
              }
            }
          }
        }
      }
    }
  }
  if (m_CacheConditions) {
    m_ConditionsUnset.insert(flag);
  }
  return value.isEmpty();
}


bool FomodInstallerDialog::testVisible(int pageIndex) const
{
  if (pageIndex < m_PageVisible.size()) {
    return m_PageVisible[pageIndex];
  }
  QWidget *page = ui->stepsStack->widget(pageIndex);
  QVariant subcond = page->property("conditional");
  if (subcond.isValid()) {
    SubCondition subc = subcond.value<SubCondition>();
    return testCondition(pageIndex, &subc);
  }
  return true;
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
    m_PageVisible.push_back(false);
    ++index;
  }
  // no more visible pages -> install
  return false;
}

void FomodInstallerDialog::widgetButtonClicked()
{
  //A button has been clicked. At the moment we do nothing with this
  //beyond checking the next button state
  updateNextbtnText();
}

void FomodInstallerDialog::updateNextbtnText()
{
  //Display 'next' or 'install' as appropriate for the next button.
  //note this can change depending on what buttons you click here.

  m_CacheConditions = false;
  auto old_PageVisible = m_PageVisible;
  ON_BLOCK_EXIT([&] () {
    m_CacheConditions = true;
    m_PageVisible = old_PageVisible;
  });

  bool isLast = true;
  for (int index = ui->stepsStack->currentIndex() + 1;
       index != ui->stepsStack->count(); ++index) {
    if (testVisible(index)) {
      isLast = false;
      break;
    }
    m_PageVisible.push_back(false);
  }

  ui->nextBtn->setText(isLast ? tr("Install") : tr("Next"));
}

void FomodInstallerDialog::on_nextBtn_clicked()
{
  if (ui->stepsStack->currentIndex() == ui->stepsStack->count() - 1) {
    this->accept();
  } else {
    if (nextPage()) {
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
    m_PageVisible.resize(previousIndex);
    ui->nextBtn->setText(tr("Next"));
  }
  if (ui->stepsStack->currentIndex() == 0) {
    ui->prevBtn->setEnabled(false);
  }
  activateCurrentPage();
}
