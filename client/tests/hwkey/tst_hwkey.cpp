// Tests for YubiKey unlock: set-up (enrolment), unlocking, wrong
// credentials, canceled touches, recovery, changing and removing the key,
// damaged settings, and that everything works offline.
//
// "FakeYubiKey" behaves like a YubiKey challenge-response slot.
// "FakeSignet" does exactly what the Signet firmware does with the login
// key (see attempt_login_256 and change_master_password_cmd in
// signet-base/firmware-hc/commands.c): AES-256 checks a known block and
// unwraps the vault key.

#include <QtTest>
#include <QSemaphore>
#include <QMutex>
#include <gcrypt.h>

#include "hwkey/loginkey.h"
#include "hwkey/yubikey.h"
#include "hwkey/loginkeybuilder.h"
#include "hwkey/rekey.h"

#ifdef QT_NETWORK_LIB
#error "The YubiKey feature and its tests must not use the network module"
#endif

using namespace HwKey;

namespace {

const int KEY_LEN = 32;

QByteArray randomBytes(int n)
{
	QByteArray b(n, 0);
	gcry_randomize(b.data(), (size_t)n, GCRY_STRONG_RANDOM);
	return b;
}

QByteArray aes(const QByteArray &key, const QByteArray &data, bool encrypt)
{
	gcry_cipher_hd_t h;
	gcry_cipher_open(&h, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CBC, 0);
	gcry_cipher_setkey(h, key.constData(), (size_t)key.size());
	QByteArray iv(16, 0);
	gcry_cipher_setiv(h, iv.constData(), 16);
	QByteArray out(data.size(), 0);
	if (encrypt) {
		gcry_cipher_encrypt(h, out.data(), (size_t)out.size(), data.constData(), (size_t)data.size());
	} else {
		gcry_cipher_decrypt(h, out.data(), (size_t)out.size(), data.constData(), (size_t)data.size());
	}
	gcry_cipher_close(h);
	return out;
}

enum { OKAY = 0, BAD_PASSWORD = 1 };

class FakeSignet
{
public:
	QByteArray hashfn;
	QByteArray salt;
	QByteArray authRand;
	QByteArray authRandCt;
	QByteArray vaultKeyCt;
	QByteArray vaultKey; // kept only so tests can check it survives

	// A Signet set up with a password only, using fast scrypt settings.
	static FakeSignet create(const QString &password)
	{
		FakeSignet s;
		ScryptParams p;
		p.log2N = 12;
		p.r = 8;
		p.p = 1;
		s.hashfn = buildHashfn(p, Config());
		s.salt = randomBytes(32);
		s.authRand = randomBytes(32);
		s.vaultKey = randomBytes(32);
		QByteArray key;
		passwordKey(password, s.hashfn, s.salt, KEY_LEN, key);
		s.authRandCt = aes(key, s.authRand, true);
		s.vaultKeyCt = aes(key, s.vaultKey, true);
		return s;
	}

	int login(const QByteArray &key) const
	{
		if (key.size() != 32 || aes(key, authRand, true) != authRandCt) {
			return BAD_PASSWORD;
		}
		return aes(key, vaultKeyCt, false) == vaultKey ? OKAY : BAD_PASSWORD;
	}

	int changeMasterPassword(const QByteArray &oldKey, const QByteArray &newKey,
				 const QByteArray &newHashfn, const QByteArray &newSalt)
	{
		if (login(oldKey) != OKAY) {
			return BAD_PASSWORD;
		}
		QByteArray vk = aes(oldKey, vaultKeyCt, false);
		authRandCt = aes(newKey, authRand, true);
		vaultKeyCt = aes(newKey, vk, true);
		hashfn = newHashfn;
		salt = newSalt;
		return OKAY;
	}
};

class FakeYubiKey : public ChallengeResponder
{
public:
	QMutex lock;
	QByteArray secret;
	int slot = 2;
	int padMode = PAD_VARIABLE;
	int delayMs = 60;       // a "touch" takes this long
	Status forced = STATUS_OK;
	quint32 serial = 1234567;
	bool block = false;     // wait until release() (for cancel tests)
	QSemaphore released;
	QAtomicInt calls;

	Status challengeResponse(int s, const QByteArray &challenge, QByteArray &response,
				 quint32 *serialOut) override
	{
		calls.fetchAndAddOrdered(1);
		QMutexLocker l(&lock);
		if (serialOut) {
			*serialOut = serial;
		}
		bool shouldBlock = block;
		int delay = delayMs;
		Status f = forced;
		QByteArray sec = secret;
		int pm = padMode;
		int mySlot = slot;
		l.unlock();
		if (shouldBlock) {
			released.acquire();
		}
		QThread::msleep((unsigned long)delay);
		if (f != STATUS_OK) {
			return f;
		}
		if (s != mySlot) {
			return STATUS_SLOT_NOT_CONFIGURED;
		}
		response = softwareResponse(sec, challenge, pm);
		return STATUS_OK;
	}
};

QString hexOf(const QByteArray &b)
{
	return QString::fromLatin1(b.toHex());
}

} // namespace

class HwKeyTest : public QObject
{
	Q_OBJECT

	FakeYubiKey *m_key;
	QByteArray m_secret;

	// Runs a Rekey to the end. Returns its status.
	int runRekey(Rekey &r, const Rekey::Request &req, bool answerBackup = true)
	{
		QSignalSpy spy(&r, SIGNAL(finished(int)));
		if (answerBackup) {
			connect(&r, &Rekey::backupKeyNeeded, &r, &Rekey::continueWithBackupKey);
		}
		r.start(req);
		if (spy.isEmpty()) {
			spy.wait(20000);
		}
		return spy.isEmpty() ? -1 : spy.first().first().toInt();
	}

	int runBuilder(LoginKeyBuilder &b, const FakeSignet &dev, const QString &password,
		       const QByteArray &recovery = QByteArray())
	{
		QSignalSpy spy(&b, SIGNAL(finished(int)));
		b.start(password, dev.hashfn, dev.salt, KEY_LEN, recovery);
		if (spy.isEmpty()) {
			spy.wait(20000);
		}
		return spy.isEmpty() ? -1 : spy.first().first().toInt();
	}

	Rekey::Request enableRequest(const FakeSignet &dev, const QString &password)
	{
		Rekey::Request req;
		req.oldPassword = password;
		req.newPassword = password;
		req.oldHashfn = dev.hashfn;
		req.oldSalt = dev.salt;
		ScryptParams sp;
		Config c;
		parseHashfn(dev.hashfn, sp, c);
		req.newScrypt = sp;
		req.newRequired = true;
		req.newSlot = 2;
		req.proof = Rekey::PROOF_SECRET;
		req.proofSecret = m_secret;
		req.keyLength = KEY_LEN;
		return req;
	}

	// Turns the YubiKey on for `dev` and checks it worked.
	void enable(FakeSignet &dev, const QString &password)
	{
		Rekey r;
		QCOMPARE(runRekey(r, enableRequest(dev, password)), (int)STATUS_OK);
		QByteArray oldKey = r.takeOldKey();
		QByteArray newKey = r.takeNewKey();
		QCOMPARE(dev.changeMasterPassword(oldKey, newKey, r.newHashfn(), r.newSalt()), (int)OKAY);
	}

private slots:
	void initTestCase()
	{
		QVERIFY(gcry_check_version(nullptr));
		gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
		m_key = new FakeYubiKey();
		setResponderForTesting(m_key);
		setMinimumTouchMsForTesting(30);
	}

	void cleanupTestCase()
	{
		setResponderForTesting(nullptr);
		delete m_key;
	}

	void init()
	{
		m_secret = QByteArray::fromHex("0123456789abcdef0123456789abcdef01234567");
		QMutexLocker l(&m_key->lock);
		m_key->secret = m_secret;
		m_key->slot = 2;
		m_key->padMode = PAD_VARIABLE;
		m_key->delayMs = 60;
		m_key->forced = STATUS_OK;
		m_key->serial = 1234567;
		m_key->block = false;
	}

	// ---- The maths ----

	void hashfnRoundTrip()
	{
		ScryptParams p;
		p.log2N = 15;
		p.r = 8;
		p.p = 1;
		Config c;
		c.required = true;
		c.slot = 1;
		c.padMode = PAD_FIXED64;
		QByteArray h = buildHashfn(p, c);
		QCOMPARE(h.size(), (int)HASHFN_SIZE);
		QCOMPARE((int)(unsigned char)h.at(0), (int)HASHFN_SCRYPT_YUBIKEY);
		ScryptParams p2;
		Config c2;
		QVERIFY(parseHashfn(h, p2, c2));
		QCOMPARE(p2.log2N, 15u);
		QCOMPARE(p2.r, 8u);
		QCOMPARE(p2.p, 1u);
		QVERIFY(c2.required);
		QCOMPARE(c2.slot, 1);
		QCOMPARE(c2.padMode, (int)PAD_FIXED64);

		// Password-only bytes look exactly like the original app's.
		QByteArray plain = buildHashfn(p, Config());
		QCOMPARE((int)plain.at(0), 1);
		QVERIFY(parseHashfn(plain, p2, c2));
		QVERIFY(!c2.required);
	}

	void damagedSettingsAreRefused()
	{
		ScryptParams p;
		p.log2N = 15;
		p.r = 8;
		p.p = 1;
		Config c;
		c.required = true;
		c.slot = 2;
		c.padMode = PAD_VARIABLE;
		QByteArray good = buildHashfn(p, c);
		ScryptParams sp;
		Config cfg;

		QByteArray bad = good; bad[5] = 3;      // no slot 3
		QVERIFY(!parseHashfn(bad, sp, cfg));
		bad = good; bad[6] = 0;                 // unknown padding
		QVERIFY(!parseHashfn(bad, sp, cfg));
		bad = good; bad[7] = 9;                 // unknown version
		QVERIFY(!parseHashfn(bad, sp, cfg));
		bad = good; bad[12] = 1;                // junk at the end
		QVERIFY(!parseHashfn(bad, sp, cfg));
		bad = good; bad[0] = 7;                 // unknown recipe
		QVERIFY(!parseHashfn(bad, sp, cfg));
		bad = good; bad[1] = 4;                 // unsafe, far too little work
		QVERIFY(!parseHashfn(bad, sp, cfg));
		QVERIFY(!parseHashfn(good.left(6), sp, cfg));

		// And nothing is produced for them.
		QByteArray key;
		bad = good; bad[5] = 3;
		passwordKey("pw", bad, randomBytes(32), KEY_LEN, key);
		QVERIFY(key.isEmpty());
	}

	void challengeIsStableAndPadded()
	{
		QByteArray salt = randomBytes(32);
		QByteArray c1 = challengeForSalt(salt);
		QCOMPARE(c1.size(), (int)CHALLENGE_SIZE);
		QCOMPARE(c1, challengeForSalt(salt));               // same salt, same challenge
		QVERIFY(c1 != challengeForSalt(randomBytes(32)));   // new salt, new challenge
		char pad = c1.at(CHALLENGE_SIZE - 1);
		QVERIFY(c1.at(CHALLENGE_DATA_SIZE - 1) != pad);
		for (int i = CHALLENGE_DATA_SIZE; i < CHALLENGE_SIZE; i++) {
			QCOMPARE(c1.at(i), pad);
		}
		QByteArray c2 = challengeForSalt(salt, alternatePadByte(c1));
		QCOMPARE(c1.left(CHALLENGE_DATA_SIZE), c2.left(CHALLENGE_DATA_SIZE));
		QVERIFY(c1 != c2);
	}

	void softwareResponseMatchesHmacSha1()
	{
		// RFC 2202 test case 1.
		QByteArray secret(20, 0x0b);
		QByteArray challenge = QByteArray("Hi There") + QByteArray(56, 0);
		QCOMPARE(hexOf(softwareResponse(secret, challenge, PAD_VARIABLE)),
			 QString("b617318655057264e28bc0b6fb378c8ef146be00"));
		// Fixed mode hashes all 64 bytes, so it differs.
		QVERIFY(softwareResponse(secret, challenge, PAD_FIXED64) !=
			softwareResponse(secret, challenge, PAD_VARIABLE));
	}

	void combineNeedsBoth()
	{
		QByteArray pw = randomBytes(32);
		QByteArray r = randomBytes(20);
		QByteArray k = combine(pw, r, KEY_LEN);
		QCOMPARE(k.size(), KEY_LEN);
		QVERIFY(k != pw);
		QByteArray otherR = r; otherR[0] = (char)(otherR.at(0) ^ 1);
		QByteArray otherPw = pw; otherPw[0] = (char)(otherPw.at(0) ^ 1);
		QVERIFY(combine(pw, otherR, KEY_LEN) != k);
		QVERIFY(combine(otherPw, r, KEY_LEN) != k);
		QVERIFY(combine(pw, QByteArray(), KEY_LEN).isEmpty());
	}

	void parseSecretFormats()
	{
		QByteArray s;
		QVERIFY(parseSecret("0123 4567-89ab:CDEF 0123 4567 89ab cdef 0123 4567", s));
		QCOMPARE(s, QByteArray::fromHex("0123456789abcdef0123456789abcdef01234567"));
		QVERIFY(!parseSecret("xyz", s));
		QVERIFY(!parseSecret("abc", s));                       // odd length
		QVERIFY(!parseSecret(QString(42, 'a'), s));            // too long
		QVERIFY(!parseSecret("", s));
	}

	// ---- Set-up (enrolment) ----

	void enrolWithSecretThenUnlock()
	{
		FakeSignet dev = FakeSignet::create("correct horse");
		enable(dev, "correct horse");

		ScryptParams sp;
		Config c;
		QVERIFY(parseHashfn(dev.hashfn, sp, c));
		QVERIFY(c.required);
		QCOMPARE(c.slot, 2);
		QCOMPARE(c.padMode, (int)PAD_VARIABLE);

		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "correct horse"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
	}

	void passwordAloneNoLongerWorks()
	{
		FakeSignet dev = FakeSignet::create("correct horse");
		enable(dev, "correct horse");
		// What the old app (or any password-only code) would send.
		QByteArray pwOnly;
		passwordKey("correct horse", dev.hashfn, dev.salt, KEY_LEN, pwOnly);
		QVERIFY(!pwOnly.isEmpty());
		QCOMPARE(dev.login(pwOnly), (int)BAD_PASSWORD);
		// The original app's recipe check doesn't know recipe 2, so it
		// sends nothing at all (first byte is no longer 0 or 1).
		QVERIFY(dev.hashfn.at(0) != 0 && dev.hashfn.at(0) != 1);
	}

	void enrolDetectsFixedPadding()
	{
		{
			QMutexLocker l(&m_key->lock);
			m_key->padMode = PAD_FIXED64;
		}
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		ScryptParams sp;
		Config c;
		QVERIFY(parseHashfn(dev.hashfn, sp, c));
		QCOMPARE(c.padMode, (int)PAD_FIXED64);
		// Recovery must give the same answer in this mode too.
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw", m_secret), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
	}

	void enrolWithWrongSecretChangesNothing()
	{
		FakeSignet dev = FakeSignet::create("pw");
		QByteArray before = dev.hashfn;
		Rekey r;
		Rekey::Request req = enableRequest(dev, "pw");
		req.proofSecret = QByteArray(20, 0x42);
		QCOMPARE(runRekey(r, req), (int)STATUS_PROOF_MISMATCH);
		QVERIFY(r.takeNewKey().isEmpty());
		QCOMPARE(dev.hashfn, before);
	}

	void enrolWithBackupKey()
	{
		FakeSignet dev = FakeSignet::create("pw");
		Rekey r;
		Rekey::Request req = enableRequest(dev, "pw");
		req.proof = Rekey::PROOF_BACKUP_KEY;
		req.proofSecret.clear();
		// When asked, "swap" to a backup key: same secret, other serial.
		connect(&r, &Rekey::backupKeyNeeded, &r, [this]() {
			QMutexLocker l(&m_key->lock);
			m_key->serial = 7654321;
		});
		QCOMPARE(runRekey(r, req), (int)STATUS_OK);
		QCOMPARE(dev.changeMasterPassword(r.takeOldKey(), r.takeNewKey(), r.newHashfn(), r.newSalt()), (int)OKAY);
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_OK); // backup key unlocks
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
	}

	void enrolBackupMustBeAnotherKey()
	{
		FakeSignet dev = FakeSignet::create("pw");
		Rekey r;
		Rekey::Request req = enableRequest(dev, "pw");
		req.proof = Rekey::PROOF_BACKUP_KEY;
		QCOMPARE(runRekey(r, req), (int)STATUS_SAME_KEY); // same serial: same key
	}

	void enrolBackupWithDifferentSecretFails()
	{
		FakeSignet dev = FakeSignet::create("pw");
		Rekey r;
		Rekey::Request req = enableRequest(dev, "pw");
		req.proof = Rekey::PROOF_BACKUP_KEY;
		connect(&r, &Rekey::backupKeyNeeded, &r, [this]() {
			QMutexLocker l(&m_key->lock);
			m_key->serial = 999;
			m_key->secret = QByteArray(20, 0x11);
		});
		QCOMPARE(runRekey(r, req), (int)STATUS_PROOF_MISMATCH);
	}

	void enrolRefusesSlotWithoutTouch()
	{
		{
			QMutexLocker l(&m_key->lock);
			m_key->delayMs = 0; // answers instantly: no touch needed
		}
		FakeSignet dev = FakeSignet::create("pw");
		Rekey r;
		QCOMPARE(runRekey(r, enableRequest(dev, "pw")), (int)STATUS_TOUCH_NOT_REQUIRED);
	}

	void enrolNeedsCorrectPasswordFirst()
	{
		FakeSignet dev = FakeSignet::create("pw");
		Rekey r;
		Rekey::Request req = enableRequest(dev, "wrong");
		req.newPassword = "wrong";
		QCOMPARE(runRekey(r, req), (int)STATUS_OK); // the app can't know yet...
		// ...but the Signet refuses, so nothing changes.
		QCOMPARE(dev.changeMasterPassword(r.takeOldKey(), r.takeNewKey(), r.newHashfn(), r.newSalt()),
			 (int)BAD_PASSWORD);
		QVERIFY(dev.hashfn.at(0) == 1);
	}

	void newKeyWithoutProofIsRejected()
	{
		FakeSignet dev = FakeSignet::create("pw");
		Rekey r;
		Rekey::Request req = enableRequest(dev, "pw");
		req.proof = Rekey::PROOF_NONE;
		QCOMPARE(runRekey(r, req), (int)STATUS_ERROR);
	}

	// ---- Unlocking ----

	void wrongPasswordFails()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "not it"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)BAD_PASSWORD);
	}

	void wrongYubiKeyFails()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		{
			QMutexLocker l(&m_key->lock);
			m_key->secret = QByteArray(20, 0x77); // someone else's key
		}
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)BAD_PASSWORD);
	}

	void missingOrUnsupportedKeyIsReported_data()
	{
		QTest::addColumn<int>("status");
		QTest::newRow("no key") << (int)STATUS_NO_KEY;
		QTest::newRow("two keys") << (int)STATUS_MULTIPLE_KEYS;
		QTest::newRow("slot not set up") << (int)STATUS_SLOT_NOT_CONFIGURED;
		QTest::newRow("not touched in time") << (int)STATUS_TOUCH_TIMEOUT;
		QTest::newRow("no smart-card service") << (int)STATUS_NO_SMARTCARD_SERVICE;
	}

	void missingOrUnsupportedKeyIsReported()
	{
		QFETCH(int, status);
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		{
			QMutexLocker l(&m_key->lock);
			m_key->forced = (Status)status;
		}
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), status);
		QVERIFY(b.takeKey().isEmpty()); // nothing to send: no password-only fallback
		QVERIFY(!statusMessage((Status)status, 2).isEmpty());
	}

	void slotChangedToNoTouchIsRefused()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		{
			QMutexLocker l(&m_key->lock);
			m_key->delayMs = 0;
		}
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_TOUCH_NOT_REQUIRED);
		QVERIFY(b.takeKey().isEmpty());
	}

	void canceledTouch()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		{
			QMutexLocker l(&m_key->lock);
			m_key->block = true;
		}
		LoginKeyBuilder b;
		QSignalSpy touch(&b, SIGNAL(touchNeeded(int)));
		QSignalSpy done(&b, SIGNAL(finished(int)));
		b.start("pw", dev.hashfn, dev.salt, KEY_LEN);
		QVERIFY(touch.wait(10000));            // the app asks for a touch
		QCOMPARE(touch.first().first().toInt(), 2);
		b.cancel();                            // user presses Cancel
		QCOMPARE(done.count(), 1);
		QCOMPARE(done.first().first().toInt(), (int)STATUS_CANCELED);
		QVERIFY(!b.busy());
		// The key is touched after all: the late answer must be ignored.
		m_key->released.release();
		QTest::qWait(300);
		QCOMPARE(done.count(), 1);
		QVERIFY(b.takeKey().isEmpty());
		{
			QMutexLocker l(&m_key->lock);
			m_key->block = false;
		}
	}

	void canceledTouchDuringEnrolmentChangesNothing()
	{
		FakeSignet dev = FakeSignet::create("pw");
		QByteArray before = dev.hashfn;
		{
			QMutexLocker l(&m_key->lock);
			m_key->block = true;
		}
		Rekey r;
		QSignalSpy touch(r.builder(), SIGNAL(touchNeeded(int)));
		QSignalSpy done(&r, SIGNAL(finished(int)));
		r.start(enableRequest(dev, "pw"));
		QVERIFY(touch.wait(10000));
		r.builder()->cancel(); // what the touch prompt's Cancel button does
		QCOMPARE(done.count(), 1);
		QCOMPARE(done.first().first().toInt(), (int)STATUS_CANCELED);
		QVERIFY(r.takeNewKey().isEmpty());
		QVERIFY(r.takeOldKey().isEmpty());
		QCOMPARE(dev.hashfn, before);
		m_key->released.release();
		QTest::qWait(300);
		QCOMPARE(done.count(), 1);
		{
			QMutexLocker l(&m_key->lock);
			m_key->block = false;
		}
	}

	// ---- Recovery ----

	void recoverWithSecretWithoutKey()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		{
			QMutexLocker l(&m_key->lock);
			m_key->forced = STATUS_NO_KEY; // key lost
		}
		int before = m_key->calls.loadAcquire();
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw", m_secret), (int)STATUS_OK);
		QCOMPARE(m_key->calls.loadAcquire(), before); // the YubiKey wasn't used
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
	}

	void recoverWithWrongSecretFails()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw", QByteArray(20, 0x33)), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)BAD_PASSWORD);
	}

	void recoverThenReplaceKey()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		// Old key lost. Program a new YubiKey (new secret) and switch to
		// it, proving the old one with the recovery secret.
		QByteArray newSecret = QByteArray::fromHex("ffeeddccbbaa99887766554433221100ffeeddcc");
		{
			QMutexLocker l(&m_key->lock);
			m_key->secret = newSecret;
			m_key->serial = 42;
		}
		Rekey r;
		Rekey::Request req = enableRequest(dev, "pw");
		req.oldHashfn = dev.hashfn;
		req.oldSalt = dev.salt;
		req.oldRecoverySecret = m_secret;
		req.proofSecret = newSecret;
		QCOMPARE(runRekey(r, req), (int)STATUS_OK);
		QCOMPARE(dev.changeMasterPassword(r.takeOldKey(), r.takeNewKey(), r.newHashfn(), r.newSalt()), (int)OKAY);

		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
		// The old secret no longer opens it.
		QCOMPARE(runBuilder(b, dev, "pw", m_secret), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)BAD_PASSWORD);
	}

	// ---- Turning off, changing the password ----

	void disableBackToPasswordOnly()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		Rekey r;
		Rekey::Request req;
		req.oldPassword = "pw";
		req.newPassword = "pw";
		req.oldHashfn = dev.hashfn;
		req.oldSalt = dev.salt;
		ScryptParams sp;
		Config c;
		parseHashfn(dev.hashfn, sp, c);
		req.newScrypt = sp;
		req.newRequired = false;
		req.keyLength = KEY_LEN;
		QCOMPARE(runRekey(r, req), (int)STATUS_OK);
		QCOMPARE(dev.changeMasterPassword(r.takeOldKey(), r.takeNewKey(), r.newHashfn(), r.newSalt()), (int)OKAY);
		QCOMPARE((int)dev.hashfn.at(0), 1);

		QByteArray pwOnly;
		passwordKey("pw", dev.hashfn, dev.salt, KEY_LEN, pwOnly);
		QCOMPARE(dev.login(pwOnly), (int)OKAY);
	}

	void changePasswordKeepsKey()
	{
		FakeSignet dev = FakeSignet::create("old pw");
		enable(dev, "old pw");
		ScryptParams sp;
		Config c;
		parseHashfn(dev.hashfn, sp, c);
		QByteArray oldSalt = dev.salt;

		Rekey r;
		Rekey::Request req;
		req.oldPassword = "old pw";
		req.newPassword = "new pw";
		req.oldHashfn = dev.hashfn;
		req.oldSalt = dev.salt;
		req.newScrypt = sp;
		req.newRequired = true;
		req.newSlot = c.slot;
		req.knownPadMode = c.padMode;
		req.proof = Rekey::PROOF_NONE;
		req.keyLength = KEY_LEN;
		QCOMPARE(runRekey(r, req), (int)STATUS_OK);
		QCOMPARE(dev.changeMasterPassword(r.takeOldKey(), r.takeNewKey(), r.newHashfn(), r.newSalt()), (int)OKAY);
		QVERIFY(dev.salt != oldSalt); // fresh salt, so a fresh challenge

		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "new pw"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
		QCOMPARE(runBuilder(b, dev, "new pw", m_secret), (int)STATUS_OK); // recovery still works
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
		QCOMPARE(runBuilder(b, dev, "old pw"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)BAD_PASSWORD);
	}

	// ---- Damage ----

	void damagedDeviceSettingsSendNothing()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		dev.hashfn[6] = 9; // corrupted
		int before = m_key->calls.loadAcquire();
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_BAD_SETTINGS);
		QVERIFY(b.takeKey().isEmpty());
		QCOMPARE(m_key->calls.loadAcquire(), before);
	}

	void damagedVaultIsNotOpened()
	{
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		dev.authRandCt[3] = (char)(dev.authRandCt.at(3) ^ 0x10); // corrupted on the device
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)BAD_PASSWORD);
	}

	// ---- The real driver, with no YubiKey plugged in ----

	void realDriverWithoutHardware()
	{
		// Build machines have no YubiKey (and Linux ones may have no
		// smart-card service). The real driver must say so calmly.
		PcscYubiKey real;
		QByteArray response;
		quint32 serial = 99;
		Status s = real.challengeResponse(2, challengeForSalt(randomBytes(32)), response, &serial);
		qInfo("Real driver said: %d", (int)s);
		QVERIFY2(s == STATUS_NO_KEY || s == STATUS_NO_SMARTCARD_SERVICE,
			 qPrintable(QString("unexpected status %1").arg((int)s)));
		QVERIFY(response.isEmpty());
		QCOMPARE(serial, 0u);
		QCOMPARE(real.challengeResponse(3, QByteArray(64, 0), response), STATUS_ERROR); // no slot 3
	}

	// ---- Offline ----

	void worksOffline()
	{
		// This test program is built without Qt's network module (see the
		// #error at the top), and the YubiKey talks over USB through the
		// local smart-card service. The full set-up and unlock below runs
		// with nothing but local code.
		FakeSignet dev = FakeSignet::create("pw");
		enable(dev, "pw");
		LoginKeyBuilder b;
		QCOMPARE(runBuilder(b, dev, "pw"), (int)STATUS_OK);
		QCOMPARE(dev.login(b.takeKey()), (int)OKAY);
	}
};

QTEST_GUILESS_MAIN(HwKeyTest)
#include "tst_hwkey.moc"
