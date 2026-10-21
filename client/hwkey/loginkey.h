#ifndef HWKEY_LOGINKEY_H
#define HWKEY_LOGINKEY_H

// How the app turns your master password (and, if switched on, your
// YubiKey's answer) into the login key it sends to the Signet.
//
// The Signet never sees the password. It stores a random "salt" and a few
// "hash function" bytes, gives them to the app, and uses whatever 32-byte
// login key comes back to decrypt the vault key it keeps inside. So the
// login key is what protects the vault. When a YubiKey is switched on, the
// login key is mixed from BOTH the scrambled password and the YubiKey's
// answer, so neither one alone can open the vault.
//
// This file only does maths. It never talks to a device, a file or the
// network, which also makes it easy to test.

#include <QByteArray>
#include <QString>

namespace HwKey {

// First byte of the device's hash-function bytes: which recipe to use.
enum HashFunction {
	HASHFN_SCRYPT_LEGACY = 0,   // very old devices: fixed settings
	HASHFN_SCRYPT = 1,          // password only
	HASHFN_SCRYPT_YUBIKEY = 2   // password + YubiKey challenge-response
};

// How the YubiKey slot reads a challenge. Most slots (YubiKey Manager's
// default) accept shorter challenges and ignore trailing padding. Some
// older setups always use all 64 bytes. The app finds out which during
// set-up and remembers it, so the recovery secret gives the same answer.
enum PadMode {
	PAD_UNKNOWN = 0,
	PAD_VARIABLE = 1,
	PAD_FIXED64 = 2
};

enum {
	CHALLENGE_SIZE = 64,       // what the YubiKey command always takes
	CHALLENGE_DATA_SIZE = 32,  // the meaningful part, the rest is padding
	RESPONSE_SIZE = 20,        // HMAC-SHA1 answer
	MAX_SECRET_SIZE = 20,      // biggest secret a YubiKey slot can hold
	HASHFN_SIZE = 16,
	HWKEY_FORMAT_VERSION = 1
};

struct ScryptParams {
	unsigned int log2N = 0;
	unsigned int r = 0;
	unsigned int p = 0;
};

// What the device's hash-function bytes say about the hardware key.
struct Config {
	bool required = false; // true: a YubiKey must be used to unlock
	int slot = 0;          // YubiKey slot 1 or 2
	int padMode = PAD_UNKNOWN;
};

// Reads the device's hash-function bytes. Returns false if they are
// damaged, unknown or ask for unsafe scrypt settings. In that case the app
// must not send anything to the device.
bool parseHashfn(const QByteArray &hashfn, ScryptParams &scrypt, Config &cfg);

// Makes new hash-function bytes with the given scrypt settings and
// hardware-key setting. cfg.required == false gives password-only bytes.
QByteArray buildHashfn(const ScryptParams &scrypt, const Config &cfg);

// Scrambles the password with scrypt using the device's settings. This is
// the whole login key for password-only devices. Leaves `key` empty if the
// settings are not acceptable.
void passwordKey(const QString &password, const QByteArray &hashfn,
		 const QByteArray &salt, int keyLength, QByteArray &key);

// The 64-byte challenge sent to the YubiKey. It is built from the device's
// salt, so every computer gets the same challenge and nothing needs to be
// saved on disk. `padByte` selects the padding; normal use leaves it at
// -1 (automatic). Set-up uses a second padding to learn the slot's PadMode.
QByteArray challengeForSalt(const QByteArray &salt, int padByte = -1);

// A padding byte that differs from both the challenge's last data byte and
// the automatic padding. Used only during set-up.
int alternatePadByte(const QByteArray &challenge);

// Mixes the scrambled password with the YubiKey's answer into the final
// login key. Both inputs are needed; changing either changes the result.
QByteArray combine(const QByteArray &passwordKey, const QByteArray &response, int keyLength);

// Computes the answer a YubiKey with this slot secret would give, without
// a YubiKey. Used for recovery and to check a typed-in secret at set-up.
QByteArray softwareResponse(const QByteArray &secret, const QByteArray &challenge, int padMode);

// Reads a slot secret typed as hexadecimal ("0123 abcd ...", spaces and
// dashes allowed). Returns false unless it is 1 to 20 bytes.
bool parseSecret(const QString &text, QByteArray &secret);

// Overwrites and empties a buffer holding something secret.
void wipe(QByteArray &data);
void wipe(QString &text);

// Compares two secrets without stopping at the first difference.
bool constantTimeEqual(const QByteArray &a, const QByteArray &b);

} // namespace HwKey

#endif
