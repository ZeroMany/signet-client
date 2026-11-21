#include "appdirs.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

static const char *s_configFile = "config.json";
static const char *s_iconPackDir = "icon-pack";

// Worked out once and then updated by setPortable(), because configDir()
// is called often (for example each time an icon is looked up).
static int s_portable = -1;

QString AppDirs::programDir()
{
	return QDir::cleanPath(QCoreApplication::applicationDirPath());
}

QString AppDirs::userConfigDir()
{
	return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
			       "/signet");
}

bool AppDirs::portableSupported()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_ANDROID)
	return false;
#else
	return qEnvironmentVariableIsEmpty("APPIMAGE");
#endif
}

// A config.json next to the program only counts if Signet wrote it in
// portable mode. This keeps an unrelated config.json (say, in a Downloads
// folder) from switching Signet into portable mode.
static bool detectPortable()
{
	if (!AppDirs::portableSupported()) {
		return false;
	}
	QFile f(AppDirs::programDir() + "/" + s_configFile);
	if (!f.open(QFile::ReadOnly)) {
		return false;
	}
	QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
	return obj.value("portable").toBool(false);
}

bool AppDirs::isPortable()
{
	if (s_portable < 0) {
		s_portable = detectPortable() ? 1 : 0;
	}
	return s_portable == 1;
}

QString AppDirs::configDir()
{
	return isPortable() ? programDir() : userConfigDir();
}

static bool sameContents(const QString &a, const QString &b)
{
	QFile fa(a), fb(b);
	if (!fa.open(QFile::ReadOnly) || !fb.open(QFile::ReadOnly)) {
		return false;
	}
	if (fa.size() != fb.size()) {
		return false;
	}
	while (!fa.atEnd()) {
		if (fa.read(65536) != fb.read(65536)) {
			return false;
		}
	}
	return true;
}

// Copies one file and checks the copy byte for byte.
static bool copyChecked(const QString &from, const QString &to)
{
	QFile::remove(to);
	return QFile::copy(from, to) && sameContents(from, to);
}

static bool copyDirChecked(const QString &from, const QString &to)
{
	QDir src(from);
	if (!QDir().mkpath(to)) {
		return false;
	}
	QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
			QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString file = it.next();
		QString target = to + "/" + src.relativeFilePath(file);
		if (!QDir().mkpath(QFileInfo(target).path()) || !copyChecked(file, target)) {
			return false;
		}
	}
	return true;
}

// Rewrites config.json with "portable" set to the given value, so that
// detectPortable() can tell a portable config from an unrelated file.
static bool writePortableFlag(const QString &configFile, bool portable)
{
	QJsonObject obj;
	QFile in(configFile);
	if (in.open(QFile::ReadOnly)) {
		obj = QJsonDocument::fromJson(in.readAll()).object();
		in.close();
	}
	obj.insert("portable", portable);
	QFile out(configFile);
	if (!out.open(QFile::WriteOnly | QFile::Truncate)) {
		return false;
	}
	QByteArray data = QJsonDocument(obj).toJson();
	return out.write(data) == data.size();
}

// Checks we can really create files in a folder: QFileInfo::isWritable()
// is unreliable on Windows (it ignores folder permissions).
static bool canWriteTo(const QString &dir)
{
	if (!QDir().mkpath(dir)) {
		return false;
	}
	QFile probe(dir + "/.signet-write-test");
	bool ok = probe.open(QFile::WriteOnly) && probe.write("x", 1) == 1;
	probe.close();
	probe.remove();
	return ok;
}

bool AppDirs::setPortable(bool portable, QString *error)
{
	if (portable == isPortable()) {
		return true;
	}
	if (portable && !portableSupported()) {
		*error = "Portable mode isn't available on this system.";
		return false;
	}
	QString from = configDir();
	QString to = portable ? programDir() : userConfigDir();

	if (!canWriteTo(to)) {
		*error = portable ?
			"Signet can't be made portable here, because it can't write to " +
			QDir::toNativeSeparators(to) + ". Move the Signet program to a folder you can write to and try again." :
			"Signet can't write to " + QDir::toNativeSeparators(to) + ".";
		return false;
	}

	QString fromConfig = from + "/" + s_configFile;
	QString toConfig = to + "/" + s_configFile;
	QString fromPack = from + "/" + s_iconPackDir;
	QString toPack = to + "/" + s_iconPackDir;
	bool hasPack = QDir(fromPack).exists();

	// Copy the icon pack first and config.json last: config.json next to
	// the program is what turns portable mode on, so it must only appear
	// once everything else is in place.
	bool ok = true;
	if (hasPack) {
		QDir(toPack).removeRecursively();
		ok = copyDirChecked(fromPack, toPack);
	}
	if (ok) {
		ok = copyChecked(fromConfig, toConfig) && writePortableFlag(toConfig, portable);
	}
	if (!ok) {
		// Undo the partial copy and leave the original files alone.
		QFile::remove(toConfig);
		if (hasPack) {
			QDir(toPack).removeRecursively();
		}
		*error = "Copying Signet's settings to " + QDir::toNativeSeparators(to) +
			 " failed, so nothing was changed.";
		return false;
	}

	s_portable = portable ? 1 : 0;

	// The copy is complete, so the originals can go. If this fails the
	// settings are still safe in the new place, so it isn't an error.
	if (hasPack) {
		QDir(fromPack).removeRecursively();
	}
	QFile::remove(fromConfig);
	if (portable) {
		// Removes the per-user signet folder only if it's now empty.
		QDir().rmdir(from);
	}
	return true;
}
