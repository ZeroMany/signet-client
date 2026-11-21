# YubiKey support (desktop only). Shared by the app and its tests.
# loginkey.cpp itself is listed in client.pro, because Android uses it too.

SOURCES += $$PWD/yubikey.cpp \
        $$PWD/yubikeyhid.cpp \
        $$PWD/yubikeyhid_platform.cpp \
        $$PWD/loginkeybuilder.cpp \
        $$PWD/rekey.cpp

HEADERS += $$PWD/loginkey.h \
        $$PWD/yubikey.h \
        $$PWD/yubikeyhid.h \
        $$PWD/loginkeybuilder.h \
        $$PWD/rekey.h

# PC/SC, the smart-card service the app uses to reach the YubiKey. It is
# part of Windows and macOS. On Linux it is pcsc-lite (package
# libpcsclite-dev to build, pcscd to run).
# The keyboard interface uses the system's HID library.
win32 {
LIBS += -lwinscard -lhid -lsetupapi
}
macx {
LIBS += -framework PCSC -framework IOKit -framework CoreFoundation
}
linux {
CONFIG += link_pkgconfig
PKGCONFIG += libpcsclite
}
