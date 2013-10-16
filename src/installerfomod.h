#ifndef INSTALLERFOMOD_H
#define INSTALLERFOMOD_H


#include <iplugininstallersimple.h>


class InstallerFomod : public MOBase::IPluginInstallerSimple
{

  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
  Q_PLUGIN_METADATA(IID "org.tannin.InstallerFomod" FILE "installerfomod.json")
#endif

public:

  InstallerFomod();

  virtual bool init(MOBase::IOrganizer *moInfo);
  virtual QString name() const;
  virtual QString author() const;
  virtual QString description() const;
  virtual MOBase::VersionInfo version() const;
  virtual bool isActive() const;
  virtual QList<MOBase::PluginSetting> settings() const;

  virtual unsigned int priority() const;
  virtual bool isManualInstaller() const;

  virtual bool isArchiveSupported(const MOBase::DirectoryTree &tree) const;
  virtual EInstallResult install(MOBase::GuessedValue<QString> &modName, MOBase::DirectoryTree &tree,
                                 QString &version, int &modID);

private:

  const MOBase::DirectoryTree *findFomodDirectory(const MOBase::DirectoryTree *tree) const;

  /**
   * @brief build a list of files (relative paths) the fomod installer may require access to
   * @param tree base tree of the archive
   * @return list of files that need to be extracted
   */
  QStringList buildFomodTree(MOBase::DirectoryTree &tree);

  void appendImageFiles(QStringList &result, MOBase::DirectoryTree *tree);
  QString getFullPath(const MOBase::DirectoryTree *tree, const MOBase::FileTreeInformation &file);
  MOBase::IPluginList::PluginState fileState(const QString &fileName);

private:

  MOBase::IOrganizer *m_MOInfo;

};

#endif // INSTALLERFOMOD_H
