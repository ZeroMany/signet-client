#-------------------------------------------------
#
# Project created by QtCreator 2016-07-17T05:31:40
#
#-------------------------------------------------

# "network" is only used for local communication between running copies
# of the app (QtSingleApplication). This app never connects to the internet.
QT       += core gui widgets network concurrent svg

android {
QT += androidextras
}

linux-g++ {
QT += x11extras
}

TARGET = signet
TEMPLATE = app
QMAKE_CFLAGS += -std=c99
# The signet-base C code relies on "common" global variables, which newer
# compilers (GCC 10+) reject by default.
QMAKE_CFLAGS += -fcommon
QMAKE_CXXFLAGS += -std=c++11 -DQTCSV_STATIC_LIB
DISTFILES += client.astylerc

# The faster SSE code only exists on Intel/AMD processors, not on
# Apple M-chip (ARM) Macs.
macx|linux-g++|win32 {
contains(QT_ARCH, x86_64): CONFIG += use_sse
}

use_sse {
QMAKE_CFLAGS += -msse4.1 -DUSE_SSE
QMAKE_CXXFLAGS += -msse4.1 -DUSE_SSE
}

# GITVERSION:
# Add a git version description to the "About" dialog if
# we are in a git repository
#system(touch $$PWD/gitversion.h)
#gitver.commands = bash $$PWD/gitversion.sh
#gitver.depends = FORCE
#about.o.depends += gitver
### This will remove any DEFINES from the client/gitversion.h file
### when the distclean target is called
#gitver_cleanup.commands = echo -n '' > $$PWD/gitversion.h
#distclean.depends += gitver_cleanup
#QMAKE_EXTRA_TARGETS += gitver gitver_cleanup distclean about.o

macx {
ICON = images/signet.icns
LIBS += -framework CoreFoundation -framework IOKit
# Libraries come from Homebrew, which lives in /opt/homebrew on
# Apple M-chip Macs.
BREW_PREFIX = $$system(brew --prefix 2>/dev/null)
isEmpty(BREW_PREFIX): BREW_PREFIX = /opt/homebrew
LIBS += -L$$BREW_PREFIX/lib -lgcrypt -lgpg-error -lz
INCLUDEPATH += $$BREW_PREFIX/include
QMAKE_INFO_PLIST = macos/Info.plist
}

linux-g++ | linux-g++-32 | linux-g++-64 {
	CONFIG+=gnu_linux
}

gnu_linux {
	linux_static_deps {
		LIBS += -l:libgcrypt.a -l:libgpg-error.a -l:libz.a -lX11
	} else {
		LIBS += -lgcrypt -lgpg-error -lz -lX11
	}
}

win32 {
QMAKE_LFLAGS = -static
LIBS += -lhid -lsetupapi -lz -lgcrypt -lgpg-error
}

#
# Signetdev sources
#

# signetdev_noemulate.c replaces signet-base's signetdev_emulate.c, so the
# app can only read passwords from a real Signet device.
SOURCES += ../signet-base/signetdev/host/signetdev.c \
    signetdev_noemulate.c


macx|gnu_linux {
HEADERS += import/passimporter.h \
    import/passimportunlockdialog.h
SOURCES += import/passimporter.cpp \
    import/passimportunlockdialog.cpp
}

win32 {
SOURCES += ../signet-base/signetdev/host/rawhid/hid_WINDOWS.c \
        ../signet-base/signetdev/host/signetdev_win32.c
}

macx {
SOURCES += ../signet-base/signetdev/host/signetdev_osx.c
}

gnu_linux {
SOURCES += ../signet-base/signetdev/host/signetdev_linux.c
}

# Android has its own USB code, which replaces signet-base's outdated
# Android and Unix files.
android {
SOURCES += android/signetdev_android.c \
        android/jni/signetactivity.cpp
HEADERS += android/signetdev_android.h
}

unix:!android {
SOURCES += ../signet-base/signetdev/host/signetdev_unix.c
SOURCES += ../signet-base/signetdev/host/signetdev_unix.h
}

#
# ESDB data sources
#
SOURCES += esdb/esdb.cpp \
    esdb/esdbtypemodule.cpp \
    esdb/account/account.cpp \
    esdb/account/esdbaccountmodule.cpp \
    esdb/bookmark/bookmark.cpp \
    esdb/bookmark/esdbbookmarkmodule.cpp \
    esdb/generic/generic.cpp \
    esdb/generic/generictypedesc.cpp \
    esdb/generic/esdbgenericmodule.cpp \
    esdb/generic/genericfields.cpp \
    esdb/generictype/esdbgenerictypemodule.cpp

HEADERS += esdb/esdb.h \
    esdb/esdbtypemodule.h \
    esdb/account/account.h \
    esdb/account/esdbaccountmodule.h \
    esdb/bookmark/bookmark.h \
    esdb/bookmark/esdbbookmarkmodule.h \
    esdb/generic/generic.h \
    esdb/generic/genericfields.h \
    esdb/generic/generictypedesc.h \
    esdb/generic/esdbgenericmodule.h \
    esdb/generictype/esdbgenerictypemodule.h

win32|gnu_linux|macx|android {

#
# ESDB GUI sources
#
SOURCES += esdb-gui/esdbmodel.cpp \
    esdb-gui/databasefield.cpp \
    esdb-gui/passwordedit.cpp \
    esdb-gui/esdbactionbar.cpp \
    esdb-gui/linefieldedit.cpp \
    esdb-gui/integerfieldedit.cpp \
    esdb-gui/textblockfieldedit.cpp \
    esdb-gui/generictext.cpp \
    esdb-gui/emphasistext.cpp \
    esdb-gui/emphasislargetext.cpp \
    esdb-gui/errortext.cpp \
    esdb-gui/notetext.cpp \
    esdb-gui/processingtext.cpp \
    esdb-gui/genericfieldedit.cpp \
    esdb-gui/genericfieldeditfactory.cpp \
    esdb-gui/genericfieldseditor.cpp \
    esdb-gui/typedescedit.cpp \
    esdb-gui/groupdatabasefield.cpp \
    esdb-gui/account/editaccount.cpp \
    esdb-gui/account/accountactionbar.cpp \
    esdb-gui/bookmark/bookmarkactionbar.cpp \
    esdb-gui/generic/genericactionbar.cpp \
    esdb-gui/generictype/generictypeactionbar.cpp \
    esdb-gui/editentrydialog.cpp \
    esdb-gui/generic/editgeneric.cpp \
    esdb-gui/bookmark/editbookmark.cpp \
    esdb-gui/generictype/editgenerictype.cpp 

HEADERS +=  esdb-gui/esdbmodel.h \
    esdb-gui/databasefield.h \
    esdb-gui/passwordedit.h \
    esdb-gui/esdbactionbar.h \
    esdb-gui/linefieldedit.h \
    esdb-gui/integerfieldedit.h \
    esdb-gui/textblockfieldedit.h \
    esdb-gui/generictext.h \
    esdb-gui/emphasistext.h \
    esdb-gui/emphasislargetext.h \
    esdb-gui/errortext.h \
    esdb-gui/notetext.h \
    esdb-gui/processingtext.h \
    esdb-gui/genericfieldedit.h \
    esdb-gui/genericfieldeditfactory.h \
    esdb-gui/genericfieldseditor.h \
    esdb-gui/typedescedit.h \
    esdb-gui/groupdatabasefield.h \
    esdb-gui/account/editaccount.h \
    esdb-gui/account/accountactionbar.h \
    esdb-gui/bookmark/bookmarkactionbar.h \
    esdb-gui/generic/genericactionbar.h \
    esdb-gui/generictype/generictypeactionbar.h \
    esdb-gui/editentrydialog.h \
    esdb-gui/generic/editgeneric.h \
    esdb-gui/bookmark/editbookmark.h \
    esdb-gui/generictype/editgenerictype.h 
}

#
# Common Misc
#

SOURCES += signetapplication.cpp \
        keygeneratorthread.cpp

HEADERS  += signetapplication.h \
        keygeneratorthread.h

#
# QtCSV
#
SOURCES += ../qtcsv/sources/contentiterator.cpp \
    ../qtcsv/sources/reader.cpp \
    ../qtcsv/sources/stringdata.cpp \
    ../qtcsv/sources/variantdata.cpp

HEADERS += ../qtcsv/sources/contentiterator.h \
    ../qtcsv/sources/filechecker.h \
    ../qtcsv/sources/symbols.h

#
#SCrypt
#
SOURCES += ../scrypt/crypto_scrypt.c \
    ../scrypt/insecure_memzero.c \
    ../scrypt/sha256.c \
    ../scrypt/warnp.c \
    ../scrypt/crypto_scrypt_smix.c

HEADERS += ../scrypt/crypto_scrypt_smix.h \
    ../scrypt/crypto_scrypt.h \
    ../scrypt/insecure_memzero.h \
    ../scrypt/sha256.h \
    ../scrypt/warnp.h



use_sse {
    SOURCES += ../scrypt/crypto_scrypt_smix_sse2.c
    HEADERS += ../scrypt/crypto_scrypt_smix_sse2.h
}

# The Android app uses the same screens as the desktop app.
gnu_linux|macx|win32|android {


INCLUDEPATH += desktop minizip

#
# Minizip sources
#
SOURCES += minizip/unzip.c \
	minizip/zip.c \
	minizip/ioapi.c \
	minizip/mztools.c

#
# Importer sources
#
SOURCES += import/entryrenamedialog.cpp \
    import/databaseimporter.cpp \
    import/databaseimportcontroller.cpp \
    import/csvimporter.cpp \
    import/csvimportconfigure.cpp

HEADERS += import/databaseimporter.h \
    import/databaseimportcontroller.h \
    import/entryrenamedialog.h \
    import/csvimporter.h \
    import/csvimportconfigure.h

!android {
SOURCES += import/keepassunlockdialog.cpp \
    import/keepassimporter.cpp
HEADERS += import/keepassunlockdialog.h \
    import/keepassimporter.h
}



#
# Desktop applicaiton sources
#
SOURCES += desktop/main.cpp \
        desktop/mainwindow.cpp \
        desktop/systemtray.cpp \
        desktop/settingsdialog.cpp \
        desktop/keyboardlayouttester.cpp \
        desktop/about.cpp \
        desktop/resetdevice.cpp \
        desktop/loggedinwidget.cpp \
        desktop/entryiconprovider.cpp \
        desktop/aspectratiopixmaplabel.cpp \
        desktop/changemasterpassword.cpp \
        desktop/searchlistbox.cpp \
        desktop/buttonwaitdialog.cpp \
        desktop/buttonwaitwidget.cpp \
        desktop/searchfilteredit.cpp \
        desktop/loginwindow.cpp \
	desktop/cleartextpasswordeditor.cpp \
	desktop/cleartextpasswordselector.cpp \
	desktop/datatypelistmodel.cpp


HEADERS +=  desktop/mainwindow.h \
        desktop/localsettings.h \
        desktop/settingsdialog.h \
        desktop/keyboardlayouttester.h \
        desktop/about.h \
        desktop/systemtray.h \
        desktop/resetdevice.h \
        desktop/loggedinwidget.h \
        desktop/entryiconprovider.h \
        desktop/aspectratiopixmaplabel.h \
        desktop/changemasterpassword.h \
        desktop/searchlistbox.h \
        desktop/buttonwaitdialog.h \
        desktop/buttonwaitwidget.h \
        desktop/searchfilteredit.h \
        desktop/loginwindow.h \
	desktop/cleartextpasswordeditor.h \
	desktop/cleartextpasswordselector.h \
	desktop/datatypelistmodel.h

#
# Qt single appliction (not needed on Android, which only ever runs one copy)
#
!android {
SOURCES += qtsingleapplication/src/qtlocalpeer.cpp \
        qtsingleapplication/src/qtsinglecoreapplication.cpp \
        qtsingleapplication/src/qtsingleapplication.cpp \
        qtsingleapplication/src/qtlockedfile.cpp

HEADERS +=  qtsingleapplication/src/qtlocalpeer.h \
    qtsingleapplication/src/QtLockedFile \
    qtsingleapplication/src/qtsinglecoreapplication.h \
    qtsingleapplication/src/QtSingleApplication \
    qtsingleapplication/src/qtsingleapplication.h \
    qtsingleapplication/src/qtlockedfile.h

win32 {
SOURCES += qtsingleapplication/src/qtlockedfile_win.cpp
}

macx:gnu_linux {
SOURCES += qtsingleapplication/src/qtlockedfile_unix.cpp
}
}

#
# KeepassX (used to import KeePass files; not built for Android yet)
#
!android {
SOURCES += ../keepassx/src/keys/CompositeKey.cpp \
    ../keepassx/src/keys/PasswordKey.cpp \
    ../keepassx/src/keys/FileKey.cpp \
    ../keepassx/src/core/AutoTypeAssociations.cpp \
    ../keepassx/src/core/Config.cpp \
    ../keepassx/src/core/Database.cpp \
    ../keepassx/src/core/DatabaseIcons.cpp \
    ../keepassx/src/core/Endian.cpp \
    ../keepassx/src/core/Entry.cpp \
    ../keepassx/src/core/EntryAttachments.cpp \
    ../keepassx/src/core/EntryAttributes.cpp \
    ../keepassx/src/core/EntrySearcher.cpp \
    ../keepassx/src/core/FilePath.cpp \
    ../keepassx/src/core/Group.cpp \
    ../keepassx/src/core/InactivityTimer.cpp \
    ../keepassx/src/core/Metadata.cpp \
    ../keepassx/src/core/SignalMultiplexer.cpp \
    ../keepassx/src/core/TimeDelta.cpp \
    ../keepassx/src/core/TimeInfo.cpp \
    ../keepassx/src/core/ToDbExporter.cpp \
    ../keepassx/src/core/Tools.cpp \
    ../keepassx/src/core/Translator.cpp \
    ../keepassx/src/core/Uuid.cpp \
    ../keepassx/src/crypto/Crypto.cpp \
    ../keepassx/src/crypto/CryptoHash.cpp \
    ../keepassx/src/crypto/Random.cpp \
    ../keepassx/src/crypto/SymmetricCipher.cpp \
    ../keepassx/src/crypto/SymmetricCipherGcrypt.cpp \
    ../keepassx/src/format/KeePass2Reader.cpp \
    ../keepassx/src/streams/HashedBlockStream.cpp \
    ../keepassx/src/streams/LayeredStream.cpp \
    ../keepassx/src/streams/qtiocompressor.cpp \
    ../keepassx/src/streams/StoreDataStream.cpp \
    ../keepassx/src/streams/SymmetricCipherStream.cpp \
    ../keepassx/src/format/CsvExporter.cpp \
    ../keepassx/src/format/KeePass1Reader.cpp \
    ../keepassx/src/format/KeePass2RandomStream.cpp \
    ../keepassx/src/format/KeePass2Repair.cpp \
    ../keepassx/src/format/KeePass2Writer.cpp \
    ../keepassx/src/format/KeePass2XmlReader.cpp \
    ../keepassx/src/format/KeePass2XmlWriter.cpp

HEADERS += ../keepassx/src/core/AutoTypeAssociations.h \
    ../keepassx/src/core/Config.h \
    ../keepassx/src/core/Database.h \
    ../keepassx/src/core/DatabaseIcons.h \
    ../keepassx/src/core/Endian.h \
    ../keepassx/src/core/Entry.h \
    ../keepassx/src/core/EntryAttachments.h \
    ../keepassx/src/core/EntryAttributes.h \
    ../keepassx/src/core/EntrySearcher.h \
    ../keepassx/src/core/Exporter.h \
    ../keepassx/src/core/FilePath.h \
    ../keepassx/src/core/Global.h \
    ../keepassx/src/core/Group.h \
    ../keepassx/src/core/InactivityTimer.h \
    ../keepassx/src/core/ListDeleter.h \
    ../keepassx/src/core/Metadata.h \
    ../keepassx/src/core/SignalMultiplexer.h \
    ../keepassx/src/core/TimeDelta.h \
    ../keepassx/src/core/TimeInfo.h \
    ../keepassx/src/core/ToDbExporter.h \
    ../keepassx/src/core/Tools.h \
    ../keepassx/src/core/Translator.h \
    ../keepassx/src/core/Uuid.h \
    ../keepassx/src/crypto/Crypto.h \
    ../keepassx/src/crypto/CryptoHash.h \
    ../keepassx/src/crypto/Random.h \
    ../keepassx/src/crypto/SymmetricCipher.h \
    ../keepassx/src/crypto/SymmetricCipherBackend.h \
    ../keepassx/src/crypto/SymmetricCipherGcrypt.h \
    ../keepassx/src/format/KeePass2.h \
    ../keepassx/src/format/KeePass2Reader.h \
    ../keepassx/src/streams/HashedBlockStream.h \
    ../keepassx/src/streams/LayeredStream.h \
    ../keepassx/src/streams/qtiocompressor.h \
    ../keepassx/src/streams/StoreDataStream.h \
    ../keepassx/src/streams/SymmetricCipherStream.h \
    ../keepassx/src/format/CsvExporter.h \
    ../keepassx/src/format/KeePass1.h \
    ../keepassx/src/format/KeePass1Reader.h \
    ../keepassx/src/format/KeePass2RandomStream.h \
    ../keepassx/src/format/KeePass2Repair.h \
    ../keepassx/src/format/KeePass2Writer.h \
    ../keepassx/src/format/KeePass2XmlReader.h \
    ../keepassx/src/format/KeePass2XmlWriter.h \
    ../keepassx/src/keys/CompositeKey_p.h \
    ../keepassx/src/keys/CompositeKey.h \
    ../keepassx/src/keys/FileKey.h \
    ../keepassx/src/keys/Key.h \
    ../keepassx/src/keys/PasswordKey.h
}

win32 {
RC_FILE = signet.rc
}

}

#
# Include path
#
INCLUDEPATH+=../qtcsv/include \
        ../qtcsv \
        ../scrypt \
        qtsingleapplication/src \
        ../keepassx/src \
        ../src \
        ../signet-base \
        esdb \
        esdb/account \
        esdb/bookmark \
        esdb/generic \
        esdb-gui/ \
        esdb-gui/account \
        esdb-gui/bookmark \
        esdb-gui/generic

RESOURCES = resources.qrc icons/icons.qrc

DISTFILES += signet.rc \
    images/signet.iconset/icon_128x128.png \
    images/signet.iconset/icon_16x16.png \
    images/signet.iconset/icon_256x256.png \
    images/signet.iconset/icon_32x32.png \
    images/signet.iconset/icon_512x512.png \
    images/signet.iconset/icon_64x64.png \
    images/chase_bank.png \
    images/clipboard.svg \
    images/signet.ico \
    images/button_press.xcf \
    images/keyboard.xcf \
    images/signet.xcf \
    images/vault.xcf \
    images/vault_open.xcf

android {
# KeePass import needs the libgcrypt library, which isn't built for
# Android yet, so that menu item is left out for now.
DEFINES += SIGNET_NO_KEEPASS
LIBS += -lz
# 64-bit ARM files are already large-file safe; this keeps minizip from
# asking for fopen64(), which older Android versions lack.
DEFINES += IOAPI_NO_64

# 64-bit ARM, which covers phones from roughly 2017 on.
ANDROID_ABIS = arm64-v8a
ANDROID_MIN_SDK_VERSION = 23
ANDROID_TARGET_SDK_VERSION = 30
ANDROID_VERSION_NAME = 0.9.17.1
ANDROID_VERSION_CODE = 3

ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android/package

DISTFILES += \
    android/package/AndroidManifest.xml \
    android/package/build.gradle \
    android/package/res/values/libs.xml \
    android/package/res/xml/device_filter.xml \
    android/package/src/com/nthdimtech/SignetActivity.java
}
