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

#ifndef FOMODINSTALLERDIALOG_H
#define FOMODINSTALLERDIALOG_H


#include <guessedvalue.h>
#include <directorytree.h>
#include <ipluginlist.h>
#include <QDialog>
#include <QAbstractButton>
#include <QXmlStreamReader>
#include <QGroupBox>
#include <QSharedPointer>
#include <QMetaType>
#include <QVariantList>
#include <functional>


namespace Ui {
class FomodInstallerDialog;
}


class ValueCondition;
class SubCondition;
class FileCondition;


class IConditionTester {
public:
  virtual bool testCondition(int maxIndex, const ValueCondition *condition) const = 0;
  virtual bool testCondition(int maxIndex, const SubCondition *condition) const = 0;
  virtual bool testCondition(int maxIndex, const FileCondition *condition) const = 0;
};


enum ConditionOperator {
  OP_AND,
  OP_OR
};

class Condition {
public:
  Condition() { }
  virtual bool test(int maxIndex, const IConditionTester *tester) const = 0;
private:
  Condition &operator=(const Condition&);
};

class ValueCondition : public Condition {
public:
  ValueCondition() : Condition(), m_Name(), m_Value() {}
  ValueCondition(const QString &name, const QString &value) : Condition(), m_Name(name), m_Value(value) { }
  virtual bool test(int maxIndex, const IConditionTester *tester) const { return tester->testCondition(maxIndex, this); }
  QString m_Name;
  QString m_Value;
};

class FileCondition : public Condition {
public:
  FileCondition() : Condition(), m_File(), m_State() {}
  FileCondition(const QString &file, const QString &state) : Condition(), m_File(file), m_State(state) {}
  virtual bool test(int maxIndex, const IConditionTester *tester) const { return tester->testCondition(maxIndex, this); }
  QString m_File;
  QString m_State;
};

class SubCondition : public Condition {
public:
  virtual bool test(int maxIndex, const IConditionTester *tester) const { return tester->testCondition(maxIndex, this); }
  ConditionOperator m_Operator;
  std::vector<Condition*> m_Conditions;
};

Q_DECLARE_METATYPE(ValueCondition)


class FileDescriptor : public QObject {
  Q_OBJECT
public:
  FileDescriptor(QObject *parent)
    : QObject(parent), m_Source(), m_Destination(), m_Priority(0), m_IsFolder(false), m_AlwaysInstall(false),
      m_InstallIfUsable(false) {}
  FileDescriptor(const FileDescriptor &reference)
    : QObject(reference.parent()), m_Source(reference.m_Source), m_Destination(reference.m_Destination),
      m_Priority(reference.m_Priority), m_IsFolder(reference.m_IsFolder), m_AlwaysInstall(reference.m_AlwaysInstall),
      m_InstallIfUsable(reference.m_InstallIfUsable) {}
  QString m_Source;
  QString m_Destination;
  int m_Priority;
  bool m_IsFolder;
  bool m_AlwaysInstall;
  bool m_InstallIfUsable;
private:
  FileDescriptor &operator=(const FileDescriptor&);
};

Q_DECLARE_METATYPE(FileDescriptor*)


class FomodInstallerDialog : public QDialog, public IConditionTester
{
  Q_OBJECT

public:
  explicit FomodInstallerDialog(const MOBase::GuessedValue<QString> &modName,
                                const QString &fomodPath,
                                const std::function<MOBase::IPluginList::PluginState (const QString &)> fileCheck,
                                QWidget *parent = 0);
  ~FomodInstallerDialog();

  void initData();

  /**
   * @return bool true if the user requested the manual dialog
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return the (user-modified) name to be used for the mod
   **/
  QString getName() const;

  /**
   * @return the version of the mod as specified in the fomod info.xml
   */
  QString getVersion() const;

  /**
   * @return the mod id as specified in the info.xml
   */
  int getModID() const;

  /**
   * @brief retrieve the updated archive tree from the dialog. The caller is responsible to delete the returned tree.
   *
   * @note This call is destructive on the input tree!
   *
   * @param tree input tree. (TODO isn't this the same as the tree passed in the constructor?)
   * @return DataTree* a new tree with only the selected options and directories arranged correctly. The caller takes custody of this pointer!
   **/
  MOBase::DirectoryTree *updateTree(MOBase::DirectoryTree *tree);

  bool hasOptions();

protected:

  virtual bool eventFilter(QObject *object, QEvent *event);

private slots:

  void on_cancelBtn_clicked();

  void on_manualBtn_clicked();

  void on_websiteLabel_linkActivated(const QString &link);

  void on_nextBtn_clicked();

  void on_prevBtn_clicked();

private:

  enum ItemOrder {
    ORDER_ASCENDING,
    ORDER_DESCENDING,
    ORDER_EXPLICIT
  };

  enum GroupType {
    TYPE_SELECTATLEASTONE,
    TYPE_SELECTATMOSTONE,
    TYPE_SELECTEXACTLYONE,
    TYPE_SELECTANY,
    TYPE_SELECTALL
  };

  enum PluginType {
    TYPE_REQUIRED,
    TYPE_RECOMMENDED,
    TYPE_OPTIONAL,
    TYPE_NOTUSABLE,
    TYPE_COULDBEUSABLE
  };

  struct Plugin {
    QString m_Name;
    QString m_Description;
    QString m_ImagePath;
    PluginType m_Type;
    SubCondition m_Condition;
    std::vector<FileDescriptor*> m_Files;
  };

  struct ConditionalInstall {
    SubCondition m_Condition;
    std::vector<FileDescriptor*> m_Files;
  };

private:

  QString readContent(QXmlStreamReader &reader);
  QString readContentUntil(QXmlStreamReader &reader, const QString &endTag);
  void parseInfo(const QByteArray &data);

  void updateNameEdit();

  static int bomOffset(const QByteArray &buffer);
  static ItemOrder getItemOrder(const QString &orderString);
  static GroupType getGroupType(const QString &typeString);
  static PluginType getPluginType(const QString &typeString);
  static bool byPriority(const FileDescriptor *LHS, const FileDescriptor *RHS);

  bool copyFileIterator(MOBase::DirectoryTree *sourceTree, MOBase::DirectoryTree *destinationTree, FileDescriptor *descriptor);
  void readFileList(QXmlStreamReader &reader, std::vector<FileDescriptor*> &fileList);
  void readPluginType(QXmlStreamReader &reader, Plugin &plugin);
  void readConditionFlags(QXmlStreamReader &reader, Plugin &plugin);
  FomodInstallerDialog::Plugin readPlugin(QXmlStreamReader &reader);
  void readPlugins(QXmlStreamReader &reader, GroupType groupType, QLayout *layout);
  void readGroup(QXmlStreamReader &reader, QLayout *layout);
  void readGroups(QXmlStreamReader &reader, QLayout *layout);
  void readVisible(QXmlStreamReader &reader, QVariantList &conditions, ConditionOperator &op);
  QGroupBox *readInstallerStep(QXmlStreamReader &reader);
  ConditionalInstall readConditionalPattern(QXmlStreamReader &reader);
  void readConditionalDependency(QXmlStreamReader &reader, SubCondition &conditional);
  void readConditionalFileInstalls(QXmlStreamReader &reader);
  void readInstallerSteps(QXmlStreamReader &reader);
  void parseModuleConfig(const QByteArray &data);
  void highlightControl(QAbstractButton *button);

  bool testCondition(int maxIndex, const QString &flag, const QString &value) const;
  virtual bool testCondition(int maxIndex, const ValueCondition *condition) const;
  virtual bool testCondition(int maxIndex, const SubCondition *condition) const;
  virtual bool testCondition(int maxIndex, const FileCondition *condition) const;
  bool testVisible(int pageIndex) const;
  bool nextPage();
  void activateCurrentPage();
  void moveTree(MOBase::DirectoryTree::Node *target, MOBase::DirectoryTree::Node *source);
  MOBase::DirectoryTree::Node *findNode(MOBase::DirectoryTree::Node *node, const QString &path, bool create);
  void copyLeaf(MOBase::DirectoryTree::Node *sourceTree, const QString &sourcePath,
                MOBase::DirectoryTree::Node *destinationTree, const QString &destinationPath);

  static QString toString(MOBase::IPluginList::PluginState state);

private:

  Ui::FomodInstallerDialog *ui;

  MOBase::GuessedValue<QString> m_ModName;

  int m_ModID;

  QString m_FomodPath;
  bool m_Manual;

  std::vector<FileDescriptor*> m_RequiredFiles;
  std::vector<ConditionalInstall> m_ConditionalInstalls;

  mutable std::map<QString, QString> m_ConditionCache;
  mutable std::set<QString> m_ConditionsUnset;

  std::function<MOBase::IPluginList::PluginState (const QString&)> m_FileCheck;

};

#endif // FOMODINSTALLERDIALOG_H
