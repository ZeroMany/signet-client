#include "entryiconprovider.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QSvgRenderer>
#include <QUrl>

#include "unzip.h"

static const char *s_builtInPackDir = ":/icons/aegis-simple-icons/";

// Limits for imported packs, so a broken or hostile zip file can't fill
// up the disk. The built-in pack is about 3 MB.
static const qint64 s_maxZipSize = 100 * 1024 * 1024;
static const qint64 s_maxIconFileSize = 5 * 1024 * 1024;
static const qint64 s_maxTotalSize = 200 * 1024 * 1024;
static const int s_maxIcons = 50000;

// Parts of a web address that never identify a service on their own
// ("accounts.google.com", "login.example.co.uk").
static const QSet<QString> s_ignoredHostParts = {
	"www", "m", "mobile", "app", "web", "login", "signin", "auth",
	"account", "accounts", "secure", "my", "mail", "online",
	"com", "co", "org", "net", "gov", "edu", "ac", "io"
};

IconPackNotifier *IconPackNotifier::get()
{
	static IconPackNotifier notifier;
	return &notifier;
}

EntryIconProvider::EntryIconProvider()
{
	reload();
}

QString EntryIconProvider::importedPackDir()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
	       "/signet/icon-pack/";
}

void EntryIconProvider::reload()
{
	m_files.clear();
	m_cache.clear();
	if (QFile::exists(importedPackDir() + "pack.json") && loadPack(importedPackDir())) {
		return;
	}
	loadPack(s_builtInPackDir);
}

static QJsonObject readPackJson(const QString &dir)
{
	QFile packFile(dir + "pack.json");
	if (!packFile.open(QFile::ReadOnly)) {
		return QJsonObject();
	}
	return QJsonDocument::fromJson(packFile.readAll()).object();
}

bool EntryIconProvider::loadPack(const QString &dir)
{
	QJsonObject pack = readPackJson(dir);
	for (const QJsonValue &v : pack.value("icons").toArray()) {
		QJsonObject icon = v.toObject();
		QString file = dir + icon.value("filename").toString();

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
	return !m_files.isEmpty();
}

EntryIconProvider::PackInfo EntryIconProvider::activePack()
{
	PackInfo info;
	EntryIconProvider provider;
	provider.m_files.clear();
	info.imported = QFile::exists(importedPackDir() + "pack.json") &&
			provider.loadPack(importedPackDir());
	QJsonObject pack = readPackJson(info.imported ? importedPackDir() : QString(s_builtInPackDir));
	info.name = pack.value("name").toString();
	info.version = pack.value("version").toInt();
	info.iconCount = pack.value("icons").toArray().size();
	return info;
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

	QIcon icon;
	if (file.endsWith(".svg", Qt::CaseInsensitive)) {
		// Draw the SVG ourselves at a few sizes, so it looks sharp anywhere
		// it is shown and doesn't depend on Qt's SVG icon plugin.
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
	} else {
		// PNG or JPEG icons (some packs use them)
		QPixmap pixmap(file);
		if (!pixmap.isNull()) {
			icon.addPixmap(pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}
	}
	m_cache.insert(file, icon);
	return icon;
}

//
// Reading the zip file
//
// The zip is read into memory and handed to minizip through these small
// functions. That way file names with any characters work on every
// system, and nothing is written until the whole pack has been checked.
//

static voidpf ZCALLBACK memOpen(voidpf opaque, const void *, int)
{
	return opaque;
}

static uLong ZCALLBACK memRead(voidpf, voidpf stream, void *buf, uLong size)
{
	qint64 n = static_cast<QIODevice *>(stream)->read(static_cast<char *>(buf), size);
	return n < 0 ? 0 : static_cast<uLong>(n);
}

static uLong ZCALLBACK memWrite(voidpf, voidpf, const void *, uLong)
{
	return 0;
}

static ZPOS64_T ZCALLBACK memTell(voidpf, voidpf stream)
{
	return static_cast<ZPOS64_T>(static_cast<QIODevice *>(stream)->pos());
}

static long ZCALLBACK memSeek(voidpf, voidpf stream, ZPOS64_T offset, int origin)
{
	QIODevice *dev = static_cast<QIODevice *>(stream);
	qint64 pos;
	switch (origin) {
	case ZLIB_FILEFUNC_SEEK_SET:
		pos = static_cast<qint64>(offset);
		break;
	case ZLIB_FILEFUNC_SEEK_CUR:
		pos = dev->pos() + static_cast<qint64>(offset);
		break;
	case ZLIB_FILEFUNC_SEEK_END:
		pos = dev->size() + static_cast<qint64>(offset);
		break;
	default:
		return -1;
	}
	return dev->seek(pos) ? 0 : -1;
}

static int ZCALLBACK memClose(voidpf, voidpf)
{
	return 0;
}

static int ZCALLBACK memError(voidpf, voidpf)
{
	return 0;
}

// Reads one file from the zip. Returns false if it's missing, damaged or
// larger than maxSize.
static bool readZipEntry(unzFile zip, const QString &name, qint64 maxSize, QByteArray *out)
{
	if (unzLocateFile(zip, name.toUtf8().constData(), 1) != UNZ_OK) {
		return false;
	}
	unz_file_info64 info;
	if (unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
		return false;
	}
	if (info.uncompressed_size > static_cast<ZPOS64_T>(maxSize)) {
		return false;
	}
	if (unzOpenCurrentFile(zip) != UNZ_OK) {
		return false;
	}
	out->resize(static_cast<int>(info.uncompressed_size));
	int n = info.uncompressed_size ? unzReadCurrentFile(zip, out->data(), out->size()) : 0;
	bool ok = (unzCloseCurrentFile(zip) == UNZ_OK) && n == out->size();
	return ok;
}

// A file name from pack.json is only accepted if it stays inside the
// pack folder: "SVG/github.svg" is fine, "../../x" or "C:/x" are not.
static bool isSafeIconPath(const QString &path)
{
	if (path.isEmpty() || path.size() > 200 || path.startsWith('/') ||
	    path.contains('\\') || path.contains(':')) {
		return false;
	}
	for (const QString &part : path.split('/')) {
		if (part.isEmpty() || part == "." || part == "..") {
			return false;
		}
	}
	QString lower = path.toLower();
	return lower.endsWith(".svg") || lower.endsWith(".png") ||
	       lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

// Finds pack.json at the top of the zip, or inside one top folder
// (as when a folder was zipped). Returns its folder, e.g. "" or "pack/".
static bool findPackPrefix(unzFile zip, QString *prefix)
{
	bool found = false;
	int bestDepth = 2;
	char nameBuf[1024];
	for (int rc = unzGoToFirstFile(zip); rc == UNZ_OK; rc = unzGoToNextFile(zip)) {
		unz_file_info64 info;
		if (unzGetCurrentFileInfo64(zip, &info, nameBuf, sizeof(nameBuf), nullptr, 0, nullptr, 0) != UNZ_OK) {
			continue;
		}
		QString name = QString::fromUtf8(nameBuf);
		if (name != "pack.json" && !name.endsWith("/pack.json")) {
			continue;
		}
		int depth = name.count('/');
		if (depth < bestDepth) {
			bestDepth = depth;
			*prefix = name.left(name.size() - QString("pack.json").size());
			found = true;
		}
	}
	return found && (prefix->isEmpty() || isSafeIconPath(*prefix + "x.svg"));
}

bool EntryIconProvider::importPack(const QString &zipPath, QString *error)
{
	QFile zipFile(zipPath);
	if (!zipFile.open(QFile::ReadOnly)) {
		*error = "The file could not be opened.";
		return false;
	}
	if (zipFile.size() > s_maxZipSize) {
		*error = "The file is too large to be an icon pack (over 100 MB).";
		return false;
	}
	QByteArray zipData = zipFile.readAll();
	zipFile.close();

	QBuffer buffer(&zipData);
	buffer.open(QIODevice::ReadOnly);
	zlib_filefunc64_def io;
	io.zopen64_file = memOpen;
	io.zread_file = memRead;
	io.zwrite_file = memWrite;
	io.ztell64_file = memTell;
	io.zseek64_file = memSeek;
	io.zclose_file = memClose;
	io.zerror_file = memError;
	io.opaque = &buffer;

	unzFile zip = unzOpen2_64("memory", &io);
	if (!zip) {
		*error = "The file is not a zip file.";
		return false;
	}

	// Step 1: read and check pack.json
	QString prefix;
	QByteArray packJson;
	if (!findPackPrefix(zip, &prefix) ||
	    !readZipEntry(zip, prefix + "pack.json", s_maxIconFileSize, &packJson)) {
		unzClose(zip);
		*error = "The zip file has no pack.json, so it isn't an icon pack in the Aegis format.";
		return false;
	}
	QJsonParseError parseError;
	QJsonDocument doc = QJsonDocument::fromJson(packJson, &parseError);
	QJsonArray icons = doc.object().value("icons").toArray();
	if (parseError.error != QJsonParseError::NoError || icons.isEmpty()) {
		unzClose(zip);
		*error = "The pack.json file in the zip is damaged or lists no icons.";
		return false;
	}
	if (icons.size() > s_maxIcons) {
		unzClose(zip);
		*error = "The icon pack lists too many icons.";
		return false;
	}

	// Step 2: read every icon pack.json lists. Icons that are missing or
	// unsafe are skipped and left out of the imported pack.json.
	QHash<QString, QByteArray> files;
	QJsonArray keptIcons;
	qint64 total = 0;
	for (const QJsonValue &v : icons) {
		QJsonObject icon = v.toObject();
		QString filename = icon.value("filename").toString();
		if (!isSafeIconPath(filename)) {
			continue;
		}
		if (!files.contains(filename)) {
			QByteArray data;
			if (!readZipEntry(zip, prefix + filename, s_maxIconFileSize, &data)) {
				continue;
			}
			total += data.size();
			if (total > s_maxTotalSize) {
				unzClose(zip);
				*error = "The icon pack is too large (over 200 MB of icons).";
				return false;
			}
			files.insert(filename, data);
		}
		keptIcons.append(icon);
	}
	unzClose(zip);
	if (keptIcons.isEmpty()) {
		*error = "None of the icons listed in pack.json were found in the zip file.";
		return false;
	}

	// Step 3: write the pack to a new folder, then swap it in, so a
	// failure halfway never leaves a half-written pack in use.
	QString finalDir = importedPackDir();
	QString finalPath = QDir::cleanPath(finalDir);
	QString newPath = finalPath + ".new";
	QString oldPath = finalPath + ".old";
	QDir(newPath).removeRecursively();
	QDir(oldPath).removeRecursively();

	QJsonObject packObj = doc.object();
	packObj.insert("icons", keptIcons);
	files.insert("pack.json", QJsonDocument(packObj).toJson());

	for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
		QString path = newPath + "/" + it.key();
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile out(path);
		if (!out.open(QFile::WriteOnly) || out.write(it.value()) != it.value().size()) {
			QDir(newPath).removeRecursively();
			*error = "The icon pack could not be saved in " + finalPath + ".";
			return false;
		}
	}

	if (QDir(finalPath).exists() && !QDir().rename(finalPath, oldPath)) {
		QDir(newPath).removeRecursively();
		*error = "The previous icon pack in " + finalPath + " could not be replaced.";
		return false;
	}
	if (!QDir().rename(newPath, finalPath)) {
		QDir().rename(oldPath, finalPath);
		QDir(newPath).removeRecursively();
		*error = "The icon pack could not be saved in " + finalPath + ".";
		return false;
	}
	QDir(oldPath).removeRecursively();
	emit IconPackNotifier::get()->changed();
	return true;
}

bool EntryIconProvider::removeImportedPack(QString *error)
{
	QDir dir(importedPackDir());
	if (dir.exists() && !dir.removeRecursively()) {
		*error = "The folder " + QDir::cleanPath(importedPackDir()) + " could not be deleted.";
		return false;
	}
	emit IconPackNotifier::get()->changed();
	return true;
}
