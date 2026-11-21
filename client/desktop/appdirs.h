#ifndef APPDIRS_H
#define APPDIRS_H

#include <QString>

//
// Decides where Signet keeps its own files (config.json and the imported
// icon pack).
//
// Normal mode: the per-user config folder, for example
//   Windows: %LOCALAPPDATA%\signet
//   Linux:   ~/.config/signet
//   Mac:     ~/Library/Preferences/signet
//
// Portable mode: the folder that holds the Signet program itself, so the
// app and its settings can be carried around together (on a USB stick, for
// example). Portable mode is on when a config.json saved by Signet in
// portable mode sits next to the program.
//
namespace AppDirs
{
	// Folder holding config.json and icon-pack/, without a trailing slash.
	QString configDir();

	// Folder of the Signet program itself.
	QString programDir();

	// The per-user folder used when not portable.
	QString userConfigDir();

	bool isPortable();

	// False on Mac (the program is inside Signet.app, and writing there
	// breaks the app's signature) and inside a read-only Linux AppImage.
	bool portableSupported();

	// Moves config.json and icon-pack/ to the other location. The files
	// are copied and checked first, and the originals are deleted only
	// once the copy is complete. On failure nothing is deleted, the mode
	// stays as it was, and *error says what went wrong.
	bool setPortable(bool portable, QString *error);
}

#endif // APPDIRS_H
