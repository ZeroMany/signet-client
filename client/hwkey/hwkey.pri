# YubiKey support (desktop only). Shared by the app and its tests.
# loginkey.cpp itself is listed in client.pro, because Android uses it too.

SOURCES += $$PWD/yubikey.cpp \
        $$PWD/loginkeybuilder.cpp \
        $$PWD/rekey.cpp

HEADERS += $$PWD/loginkey.h \
        $$PWD/yubikey.h \
        $$PWD/loginkeybuilder.h \
        $$PWD/rekey.h

# PC/SC, the smart-card service the app uses to reach the YubiKey. It is
# part of Windows and macOS. On Linux it is pcsc-lite (package
# libpcsclite-dev to build, pcscd to run).
win32 {
LIBS += -lwinscard
}
macx {
LIBS += -framework PCSC
}
linux {
CONFIG += link_pkgconfig
PKGCONFIG += libpcsclite
}
