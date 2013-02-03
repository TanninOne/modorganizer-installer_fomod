#ifndef INSTALLERFOMOD_H
#define INSTALLERFOMOD_H


#include <iplugininstallersimple.h>


class InstallerFomod : public IPluginInstallerSimple
{

  Q_OBJECT
  Q_INTERFACES(IPlugin IPluginInstaller IPluginInstallerSimple)

public:

  InstallerFomod();

  virtual bool init(IOrganizer *moInfo);
  virtual QString name() const;
  virtual QString author() const;
  virtual QString description() const;
  virtual VersionInfo version() const;
  virtual bool isActive() const;
  virtual QList<PluginSetting> settings() const;

  virtual unsigned int priority() const;
  virtual bool isManualInstaller() const;

  virtual bool isArchiveSupported(const DirectoryTree &tree) const;
  virtual EInstallResult install(QString &modName, DirectoryTree &tree);

private:

  const DirectoryTree *findFomodDirectory(const DirectoryTree *tree) const;

  /**
   * @brief build a list of files (relative paths) the fomod installer may require access to
   * @param tree base tree of the archive
   * @return list of files that need to be extracted
   */
  QStringList buildFomodTree(DirectoryTree &tree);

  void appendImageFiles(QStringList &result, DirectoryTree *tree);
  QString getFullPath(const DirectoryTree *tree, const FileTreeInformation &file);

private:

  const IOrganizer *m_MOInfo;

};

#endif // INSTALLERFOMOD_H
