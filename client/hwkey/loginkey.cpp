#include "loginkey.h"

#include <gcrypt.h>
#include <cstring>

extern "C" {
#include "crypto_scrypt.h"
}

namespace HwKey {

namespace {

// Labels keep the different uses of the same inputs apart.
const char CHALLENGE_LABEL[] = "signet-yubikey-challenge-v1";
const char LOGIN_LABEL[] = "signet-yubikey-login-v1";

// Same limits the app has always used: a fake or tampered device could
// otherwise ask for almost no work (making the password easy to crack from
// what we send) or for so much that the computer runs out of memory.
bool scryptParamsSafe(const ScryptParams &s)
{
	return s.log2N >= 12 && s.log2N <= 20 &&
	       s.r >= 8 && s.r <= 32 &&
	       s.p >= 1 && s.p <= 16;
}

QByteArray hmac(int algo, const QByteArray &key, const QByteArray &data)
{
	gcry_md_hd_t h;
	if (gcry_md_open(&h, algo, GCRY_MD_FLAG_HMAC) != 0) {
		return QByteArray();
	}
	if (gcry_md_setkey(h, key.constData(), (size_t)key.size()) != 0) {
		gcry_md_close(h);
		return QByteArray();
	}
	gcry_md_write(h, data.constData(), (size_t)data.size());
	const unsigned char *out = gcry_md_read(h, algo);
	QByteArray result((const char *)out, (int)gcry_md_get_algo_dlen(algo));
	gcry_md_close(h); // also wipes gcrypt's own copy
	return result;
}

void scrypt(const QString &password, const QByteArray &salt,
	    unsigned int N, unsigned int r, unsigned int p, QByteArray &key)
{
	QByteArray utf8 = password.toUtf8();
	if (crypto_scrypt((const uint8_t *)utf8.constData(), (size_t)utf8.size(),
			  (const uint8_t *)salt.constData(), (size_t)salt.size(),
			  N, r, p, (uint8_t *)key.data(), (size_t)key.size()) != 0) {
		wipe(key);
	}
	wipe(utf8);
}

} // namespace

void wipe(QByteArray &data)
{
	if (!data.isEmpty()) {
		// volatile stops the compiler from skipping the overwrite
		volatile char *p = data.data();
		for (int i = 0; i < data.size(); i++) {
			p[i] = 0;
		}
	}
	data.clear();
}

void wipe(QString &text)
{
	if (!text.isEmpty()) {
		text.fill(QChar(0));
	}
	text.clear();
}

bool constantTimeEqual(const QByteArray &a, const QByteArray &b)
{
	if (a.size() != b.size()) {
		return false;
	}
	unsigned char diff = 0;
	for (int i = 0; i < a.size(); i++) {
		diff |= (unsigned char)(a.at(i) ^ b.at(i));
	}
	return diff == 0;
}

bool parseHashfn(const QByteArray &hashfn, ScryptParams &scrypt, Config &cfg)
{
	scrypt = ScryptParams();
	cfg = Config();
	if (hashfn.size() < 5) {
		return false;
	}
	const unsigned char *b = (const unsigned char *)hashfn.constData();
	int fn = b[0];
	if (fn != HASHFN_SCRYPT && fn != HASHFN_SCRYPT_YUBIKEY) {
		return false;
	}
	scrypt.log2N = b[1];
	scrypt.r = b[2] + ((unsigned int)b[3] << 8);
	scrypt.p = b[4];
	if (!scryptParamsSafe(scrypt)) {
		return false;
	}
	if (fn == HASHFN_SCRYPT) {
		return true;
	}

	// Password + YubiKey. These bytes are written only by this app, so
	// anything unexpected means they are damaged: refuse rather than guess.
	if (hashfn.size() < HASHFN_SIZE) {
		return false;
	}
	int slot = b[5];
	int padMode = b[6];
	int version = b[7];
	if ((slot != 1 && slot != 2) ||
	    (padMode != PAD_VARIABLE && padMode != PAD_FIXED64) ||
	    version != HWKEY_FORMAT_VERSION) {
		return false;
	}
	for (int i = 8; i < HASHFN_SIZE; i++) {
		if (b[i] != 0) {
			return false;
		}
	}
	cfg.required = true;
	cfg.slot = slot;
	cfg.padMode = padMode;
	return true;
}

QByteArray buildHashfn(const ScryptParams &scrypt, const Config &cfg)
{
	QByteArray h(HASHFN_SIZE, 0);
	h[0] = (char)(cfg.required ? HASHFN_SCRYPT_YUBIKEY : HASHFN_SCRYPT);
	h[1] = (char)scrypt.log2N;
	h[2] = (char)(scrypt.r & 0xff);
	h[3] = (char)((scrypt.r >> 8) & 0xff);
	h[4] = (char)scrypt.p;
	if (cfg.required) {
		h[5] = (char)cfg.slot;
		h[6] = (char)cfg.padMode;
		h[7] = (char)HWKEY_FORMAT_VERSION;
	}
	return h;
}

void passwordKey(const QString &password, const QByteArray &hashfn,
		 const QByteArray &salt, int keyLength, QByteArray &key)
{
	wipe(key);
	if (hashfn.isEmpty() || keyLength <= 0) {
		return;
	}
	key = QByteArray(keyLength, 0);
	if ((unsigned char)hashfn.at(0) == HASHFN_SCRYPT_LEGACY) {
		// Very old devices. Kept exactly as before.
		scrypt(password, QByteArray("rand", 4), 1u << 12, 32, 1, key);
		return;
	}
	ScryptParams s;
	Config cfg;
	if (!parseHashfn(hashfn, s, cfg) || salt.size() < 16) {
		wipe(key);
		return;
	}
	scrypt(password, salt, 1u << s.log2N, s.r, s.p, key);
}

QByteArray challengeForSalt(const QByteArray &salt, int padByte)
{
	QByteArray data = hmac(GCRY_MD_SHA256, salt, QByteArray(CHALLENGE_LABEL));
	if (data.size() != CHALLENGE_DATA_SIZE) {
		return QByteArray();
	}
	if (padByte < 0) {
		// Pad with a byte different from the last data byte, so a slot
		// that strips padding sees exactly the 32 data bytes.
		padByte = (data.at(CHALLENGE_DATA_SIZE - 1) == 0) ? 1 : 0;
	}
	return data + QByteArray(CHALLENGE_SIZE - CHALLENGE_DATA_SIZE, (char)padByte);
}

int alternatePadByte(const QByteArray &challenge)
{
	unsigned char last = (unsigned char)challenge.at(CHALLENGE_DATA_SIZE - 1);
	return last == 0xA5 ? 0x5A : 0xA5;
}

QByteArray combine(const QByteArray &passwordKey, const QByteArray &response, int keyLength)
{
	if (passwordKey.isEmpty() || response.size() != RESPONSE_SIZE ||
	    keyLength <= 0 || keyLength > 32) {
		return QByteArray();
	}
	QByteArray data = QByteArray(LOGIN_LABEL) + response;
	QByteArray out = hmac(GCRY_MD_SHA256, passwordKey, data);
	wipe(data);
	if (out.size() < keyLength) {
		wipe(out);
		return QByteArray();
	}
	if (out.size() > keyLength) {
		QByteArray shortened = out.left(keyLength);
		wipe(out);
		return shortened;
	}
	return out;
}

QByteArray softwareResponse(const QByteArray &secret, const QByteArray &challenge, int padMode)
{
	if (secret.isEmpty() || secret.size() > MAX_SECRET_SIZE ||
	    challenge.size() != CHALLENGE_SIZE) {
		return QByteArray();
	}
	int len = CHALLENGE_SIZE;
	if (padMode == PAD_VARIABLE) {
		// Like the YubiKey: drop trailing bytes equal to the last one.
		char last = challenge.at(CHALLENGE_SIZE - 1);
		while (len > 0 && challenge.at(len - 1) == last) {
			len--;
		}
	} else if (padMode != PAD_FIXED64) {
		return QByteArray();
	}
	// A secret shorter than 20 bytes is stored zero-padded on the YubiKey.
	// HMAC pads short keys with zeros anyway, so both give the same answer.
	return hmac(GCRY_MD_SHA1, secret, challenge.left(len));
}

bool parseSecret(const QString &text, QByteArray &secret)
{
	wipe(secret);
	QByteArray hex;
	for (QChar c : text) {
		if (c.isSpace() || c == '-' || c == ':') {
			continue;
		}
		char ch = c.toLatin1();
		bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
		if (!ok) {
			wipe(hex);
			return false;
		}
		hex.append(ch);
	}
	if (hex.isEmpty() || (hex.size() % 2) != 0 || hex.size() > MAX_SECRET_SIZE * 2) {
		wipe(hex);
		return false;
	}
	secret = QByteArray::fromHex(hex);
	wipe(hex);
	return true;
}

} // namespace HwKey
