#ifndef ENTRYICONPROVIDER_H
#define ENTRYICONPROVIDER_H

#include <QHash>
#include <QIcon>
#include <QString>

//
// Picks an icon for a password entry from the bundled
// "aegis-simple-icons" pack (see client/icons/README.md).
//
// The pack is compiled into the app, so no network access is needed.
//
class EntryIconProvider
{
public:
	EntryIconProvider();

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

private:
	// normalized icon name -> resource path of its SVG file
	QHash<QString, QString> m_files;
	// resource path -> rendered icon (filled in as icons are first used)
	QHash<QString, QIcon> m_cache;

	QString findFile(const QString &title, const QString &url) const;
	QString lookupHost(const QString &host) const;
	QIcon loadIcon(const QString &file);
};

#endif // ENTRYICONPROVIDER_H
