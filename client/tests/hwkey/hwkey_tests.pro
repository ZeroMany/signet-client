# Tests for the YubiKey unlock feature. They use a pretend YubiKey and a
# pretend Signet (a copy of how the device wraps its vault key), so they
# run anywhere, with no hardware and no network.
#
# Build and run:  qmake && make && ./hwkey_tests   (Windows: release\hwkey_tests.exe)

# Deliberately no "network" and no "gui": this proves the feature can't
# reach the network.
QT = core testlib
CONFIG += console testcase
CONFIG -= app_bundle
TARGET = hwkey_tests
TEMPLATE = app
QMAKE_CXXFLAGS += -std=c++11

CLIENT = $$PWD/../..

SOURCES += tst_hwkey.cpp \
        $$CLIENT/hwkey/loginkey.cpp \
        $$CLIENT/../scrypt/crypto_scrypt.c \
        $$CLIENT/../scrypt/insecure_memzero.c \
        $$CLIENT/../scrypt/sha256.c \
        $$CLIENT/../scrypt/warnp.c \
        $$CLIENT/../scrypt/crypto_scrypt_smix.c

include($$CLIENT/hwkey/hwkey.pri)

INCLUDEPATH += $$CLIENT $$CLIENT/../scrypt
QMAKE_CFLAGS += -std=gnu99

macx {
BREW_PREFIX = $$system(brew --prefix 2>/dev/null)
isEmpty(BREW_PREFIX): BREW_PREFIX = /opt/homebrew
LIBS += -L$$BREW_PREFIX/lib
INCLUDEPATH += $$BREW_PREFIX/include
}
win32 {
QMAKE_LFLAGS += -static
}
LIBS += -lgcrypt -lgpg-error
