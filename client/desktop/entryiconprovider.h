#ifndef ENTRYICONPROVIDER_H
#define ENTRYICONPROVIDER_H

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

//
// Picks an icon for a password entry from an icon pack in the Aegis
// format (see client/icons/README.md).
//
// The app has a built-in pack ("aegis-simple-icons"). You can also import
// your own pack from a .zip file in Settings. An imported pack is used
// instead of the built-in one until you remove it.
//
// Everything is read from files on this computer, so no network access
// is needed.
//
class EntryIconProvider
{
public:
	EntryIconProvider();

	// Loads the pack again (after a pack was imported or removed).
	void reload();

	// Returns the best icon for an entry with this title and URL,
	// or a null QIcon if nothing in the pack matches.
	QIcon iconFor(const QString &title, const QString &url);

	int iconCount() const
	{
		return m_files.size();
	}

	// Lower-cases the text and keeps only letters and digits,
	// so "Dev.to", "dev to" and "DEVTO" all become "devto".
	static QString normalize(const QString &text);

	// What the Settings window shows about the pack in use.
	struct PackInfo {
		QString name;
		int version = 0;
		int iconCount = 0;
		bool imported = false;
	};
	static PackInfo activePack();

	// Copies an icon pack from a .zip file into the app's settings folder.
	// Returns false and sets *error if the file isn't a usable icon pack.
	// The pack in use is left as it was in that case.
	static bool importPack(const QString &zipPath, QString *error);

	// Deletes the imported pack, so the built-in pack is used again.
	static bool removeImportedPack(QString *error);

	// Folder where an imported pack is kept.
	static QString importedPackDir();

private:
	// normalized icon name -> path of its image file
	QHash<QString, QString> m_files;
	// image path -> rendered icon (filled in as icons are first used)
	QHash<QString, QIcon> m_cache;

	bool loadPack(const QString &dir);
	QString findFile(const QString &title, const QString &url) const;
	QString lookupHost(const QString &host) const;
	QIcon loadIcon(const QString &file);
};

//
// Sends changed() whenever an icon pack is imported or removed, so open
// windows can redraw their icons.
//
class IconPackNotifier : public QObject
{
	Q_OBJECT
public:
	static IconPackNotifier *get();
signals:
	void changed();
};

#endif // ENTRYICONPROVIDER_H
