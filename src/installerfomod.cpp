#include "installerfomod.h"
#include "fomodinstallerdialog.h"

#include <report.h>
#include <iinstallationmanager.h>
#include <utility.h>

#include <QtPlugin>
#include <QStringList>
#include <QImageReader>
#include <QDebug>


using namespace MOBase;


InstallerFomod::InstallerFomod()
  : m_MOInfo(NULL)
{
}

bool InstallerFomod::init(IOrganizer *moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString InstallerFomod::name() const
{
  return "Fomod Installer";
}

QString InstallerFomod::author() const
{
  return "Tannin";
}

QString InstallerFomod::description() const
{
  return tr("Installer for xml based fomod archives. This probably has worse compatibility than the NCC based plugin.");
}

VersionInfo InstallerFomod::version() const
{
  return VersionInfo(1, 5, 0, VersionInfo::RELEASE_FINAL);
}

bool InstallerFomod::isActive() const
{
  return m_MOInfo->pluginSetting(name(), "enabled").toBool();
}

QList<PluginSetting> InstallerFomod::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting("enabled", "check to enable this plugin", QVariant(true)));
  result.push_back(PluginSetting("prefer", "prefer this over the NCC based plugin", QVariant(true)));
  return result;
}

unsigned int InstallerFomod::priority() const
{
  return m_MOInfo->pluginSetting(name(), "prefer").toBool() ? 110 : 90;
}


bool InstallerFomod::isManualInstaller() const
{
  return false;
}


const DirectoryTree *InstallerFomod::findFomodDirectory(const DirectoryTree *tree) const
{
  for (DirectoryTree::const_node_iterator iter = tree->nodesBegin();
       iter != tree->nodesEnd(); ++iter) {
    const QString &dirName = (*iter)->getData().name;
    if (dirName.compare("fomod", Qt::CaseInsensitive) == 0) {
      return *iter;
    }
  }
  if ((tree->numNodes() == 1) && (tree->numLeafs() == 0)) {
    return findFomodDirectory(*tree->nodesBegin());
  }
  return NULL;
}


bool InstallerFomod::isArchiveSupported(const DirectoryTree &tree) const
{
  const DirectoryTree *fomodDir = findFomodDirectory(&tree);
  if (fomodDir != NULL) {
    for (DirectoryTree::const_leaf_iterator fileIter = fomodDir->leafsBegin();
         fileIter != fomodDir->leafsEnd(); ++fileIter) {
      if (fileIter->getName().compare("ModuleConfig.xml", Qt::CaseInsensitive) == 0) {
        return true;
      }
    }
  }
  return false;
}


QString InstallerFomod::getFullPath(const DirectoryTree *tree, const FileTreeInformation &file)
{
  QString result;
  const DirectoryTree *current = tree;
  while (current != NULL) {
    result.prepend(current->getData().name + "/");
    current = current->getParent();
  }
  result.append(file.getName());
  return result;
}


void InstallerFomod::appendImageFiles(QStringList &result, DirectoryTree *tree)
{
  for (auto iter = tree->leafsBegin(); iter != tree->leafsEnd(); ++iter) {
    if ((iter->getName().endsWith(".png", Qt::CaseInsensitive)) ||
        (iter->getName().endsWith(".jpg", Qt::CaseInsensitive)) ||
        (iter->getName().endsWith(".gif", Qt::CaseInsensitive)) ||
        (iter->getName().endsWith(".bmp", Qt::CaseInsensitive))) {
      result.append(getFullPath(tree, *iter));
    }
  }

  for (auto iter = tree->nodesBegin(); iter != tree->nodesEnd(); ++iter) {
    appendImageFiles(result, *iter);
  }
}


QStringList InstallerFomod::buildFomodTree(DirectoryTree &tree)
{
  QStringList result;
  const DirectoryTree *fomodTree = findFomodDirectory(&tree);
  for (auto iter = fomodTree->leafsBegin(); iter != fomodTree->leafsEnd(); ++iter) {
    if ((iter->getName().compare("info.xml", Qt::CaseInsensitive) == 0) ||
        (iter->getName().compare("ModuleConfig.xml", Qt::CaseInsensitive) == 0)) {
      result.append(getFullPath(fomodTree, *iter));
    }
  }

  appendImageFiles(result, &tree);

  return result;
}


IPluginList::PluginState InstallerFomod::fileState(const QString &fileName)
{
  return m_MOInfo->pluginList()->state(fileName);
}


IPluginInstaller::EInstallResult InstallerFomod::install(GuessedValue<QString> &modName, DirectoryTree &tree,
                                                         QString &version, int &modID)
{
  QStringList installerFiles = buildFomodTree(tree);
  manager()->extractFiles(installerFiles, false);

  try {
    const DirectoryTree *fomodTree = findFomodDirectory(&tree);

    QString fomodPath;
    const DirectoryTree *current = fomodTree->getParent();
    while (current != NULL) {
      fomodPath.prepend(current->getData().name);
      current = current->getParent();
    }
    FomodInstallerDialog dialog(modName, fomodPath, std::bind(&InstallerFomod::fileState, this, std::placeholders::_1));
    dialog.initData();
    if (!dialog.getVersion().isEmpty()) {
      version = dialog.getVersion();
    }
    if (dialog.getModID() != -1) {
      modID = dialog.getModID();
    }

    if (!dialog.hasOptions() || (dialog.exec() == QDialog::Accepted)) {
      modName.update(dialog.getName(), GUESS_USER);
      DirectoryTree *newTree = dialog.updateTree(&tree);
      tree = *newTree;
      delete newTree;

      return IPluginInstaller::RESULT_SUCCESS;
    } else {
      if (dialog.manualRequested()) {
        modName.update(dialog.getName(), GUESS_USER);
        return IPluginInstaller::RESULT_MANUALREQUESTED;
      } else {
        return IPluginInstaller::RESULT_FAILED;
      }
    }
  } catch (const std::exception &e) {
    reportError(tr("Installation as fomod failed: %1").arg(e.what()));
    return IPluginInstaller::RESULT_MANUALREQUESTED;
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
Q_EXPORT_PLUGIN2(installerFomod, InstallerFomod)
#endif


std::vector<unsigned int> InstallerFomod::activeProblems() const
{
  std::vector<unsigned int> result;
  QList<QByteArray> formats = QImageReader::supportedImageFormats();
  if (!formats.contains("jpg")) {
    result.push_back(PROBLEM_IMAGETYPE_UNSUPPORTED);
  }
  return result;
}

QString InstallerFomod::shortDescription(unsigned int key) const
{
  switch (key) {
    case PROBLEM_IMAGETYPE_UNSUPPORTED:
      return tr("image formats not supported.");
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

QString InstallerFomod::fullDescription(unsigned int key) const
{
  switch (key) {
    case PROBLEM_IMAGETYPE_UNSUPPORTED:
      return tr("This indicates that files from dlls/imageformats are missing from your MO installation or outdated. "
                "Images in installers may not be displayed. Please re-install MO");
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

bool InstallerFomod::hasGuidedFix(unsigned int) const
{
  return false;
}

void InstallerFomod::startGuidedFix(unsigned int) const
{
}
