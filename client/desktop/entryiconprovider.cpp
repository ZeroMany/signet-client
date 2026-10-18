#include "entryiconprovider.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QSvgRenderer>
#include <QUrl>

static const char *s_packDir = ":/icons/aegis-simple-icons/";

// Parts of a web address that never identify a service on their own
// ("accounts.google.com", "login.example.co.uk").
static const QSet<QString> s_ignoredHostParts = {
	"www", "m", "mobile", "app", "web", "login", "signin", "auth",
	"account", "accounts", "secure", "my", "mail", "online",
	"com", "co", "org", "net", "gov", "edu", "ac", "io"
};

EntryIconProvider::EntryIconProvider()
{
	QFile packFile(QString(s_packDir) + "pack.json");
	if (!packFile.open(QFile::ReadOnly)) {
		return;
	}
	QJsonObject pack = QJsonDocument::fromJson(packFile.readAll()).object();
	for (const QJsonValue &v : pack.value("icons").toArray()) {
		QJsonObject icon = v.toObject();
		QString file = QString(s_packDir) + icon.value("filename").toString();

		// Index each icon by its display name ("Dev.to"), by any extra
		// names Aegis lists for it, and by its file name ("devdotto").
		QStringList names;
		names << icon.value("name").toString();
		for (const QJsonValue &issuer : icon.value("issuer").toArray()) {
			names << issuer.toString();
		}
		names << QFileInfo(file).completeBaseName();

		for (const QString &name : names) {
			QString key = normalize(name);
			if (!key.isEmpty() && !m_files.contains(key)) {
				m_files.insert(key, file);
			}
		}
	}
}

QString EntryIconProvider::normalize(const QString &text)
{
	QString out;
	for (QChar c : text.toLower()) {
		if (c.isLetterOrNumber()) {
			out.append(c);
		}
	}
	return out;
}

QIcon EntryIconProvider::iconFor(const QString &title, const QString &url)
{
	QString file = findFile(title, url);
	if (file.isEmpty()) {
		return QIcon();
	}
	return loadIcon(file);
}

QString EntryIconProvider::findFile(const QString &title, const QString &url) const
{
	// 1. The whole title names a service: "GitHub", "Dev.to"
	QString key = normalize(title);
	if (m_files.contains(key)) {
		return m_files.value(key);
	}

	// 2. The web address names a service: "https://accounts.google.com"
	for (const QString &text : {url, title}) {
		QString candidate = text.trimmed();
		if (candidate.isEmpty() || candidate.contains(' ')) {
			continue;
		}
		if (!candidate.contains("://")) {
			candidate.prepend("https://");
		}
		QString host = QUrl(candidate).host();
		if (host.contains('.')) {
			QString found = lookupHost(host);
			if (!found.isEmpty()) {
				return found;
			}
		}
	}

	// 3. The first word of the title names a service: "Google work account"
	QString firstWord = normalize(title.section(' ', 0, 0, QString::SectionSkipEmpty));
	if (firstWord.size() >= 3 && m_files.contains(firstWord)) {
		return m_files.value(firstWord);
	}
	return QString();
}

QString EntryIconProvider::lookupHost(const QString &host) const
{
	QStringList parts = host.toLower().split('.', QString::SkipEmptyParts);
	if (parts.size() < 2) {
		return QString();
	}
	parts.removeLast(); // the ending: ".com", ".uk", ...

	// Check from right to left, so "mail.google.com" finds "google"
	// before "mail".
	for (int i = parts.size() - 1; i >= 0; i--) {
		QString part = normalize(parts.at(i));
		if (s_ignoredHostParts.contains(part)) {
			continue;
		}
		if (m_files.contains(part)) {
			return m_files.value(part);
		}
	}
	return QString();
}

QIcon EntryIconProvider::loadIcon(const QString &file)
{
	if (m_cache.contains(file)) {
		return m_cache.value(file);
	}

	// Draw the SVG ourselves at a few sizes, so it looks sharp anywhere
	// it is shown and doesn't depend on Qt's SVG icon plugin.
	QIcon icon;
	QSvgRenderer renderer(file);
	if (renderer.isValid()) {
		for (int size : {16, 24, 32, 48, 64}) {
			QPixmap pixmap(size, size);
			pixmap.fill(Qt::transparent);
			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing);
			renderer.render(&painter);
			painter.end();
			icon.addPixmap(pixmap);
		}
	}
	m_cache.insert(file, icon);
	return icon;
}
