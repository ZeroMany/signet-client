#ifndef LOCALSETTINGS_H
#define LOCALSETTINGS_H

#include <QVector>
#include <QMap>
#include <QString>
#include <QDateTime>

extern "C" {
#include "signetdev/host/signetdev.h"
}

typedef QVector<struct signetdev_key> keyboardLayout;

struct localSettings {
	bool localBackups;
	QString localBackupPath;
	int localBackupInterval;
	bool removableBackups;
	QString removableBackupPath;
	QString removableBackupVolume;
	int removableBackupInterval;
	QDateTime lastRemoveableBackup;
	QMap<QString, keyboardLayout> keyboardLayouts;
	QString activeKeyboardLayout;
	QByteArray windowGeometry;
	bool minimizeToTray;
	// Log out after this many minutes without using the app (0 = never)
	int autoLockMinutes;
};

#endif // LOCALSETTINGS_H
