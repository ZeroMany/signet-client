#ifndef HWKEY_REKEY_H
#define HWKEY_REKEY_H

// Works out everything needed to change how the vault is locked: turning
// the YubiKey on, swapping it for another, turning it off, or changing the
// master password while keeping the YubiKey.
//
// It produces the current login key, the new login key, and the new salt
// and hash-function bytes. The window then hands these to the Signet with
// signetdev_change_master_password(), and the user confirms on the device.
// The Signet re-wraps its vault key under the new login key; the vault
// itself stays encrypted the whole time.

#include <QObject>
#include <QByteArray>
#include <QString>

#include "loginkey.h"
#include "loginkeybuilder.h"

namespace HwKey {

class Rekey : public QObject
{
	Q_OBJECT
public:
	// How the user shows they can still get in if the YubiKey is lost.
	enum Proof {
		PROOF_NONE,       // only when the same YubiKey slot stays in use
		PROOF_SECRET,     // they typed the slot's secret
		PROOF_BACKUP_KEY  // they touched a second YubiKey with the same secret
	};
	enum Step {
		STEP_NONE,
		STEP_CURRENT_LOGIN,   // unlocking with the current settings
		STEP_NEW_PASSWORD,    // scrambling the (new) password
		STEP_NEW_KEY,         // first touch of the new key
		STEP_NEW_KEY_AGAIN,   // second touch, to learn how the slot pads challenges
		STEP_BACKUP_KEY,      // touch of the backup key
		STEP_DONE
	};
	struct Request {
		QString oldPassword;
		QByteArray oldHashfn;
		QByteArray oldSalt;
		QByteArray oldRecoverySecret; // empty: use the YubiKey if one is needed
		QString newPassword;
		ScryptParams newScrypt;
		bool newRequired = false;
		int newSlot = 0;
		int knownPadMode = PAD_UNKNOWN; // PAD_UNKNOWN: find out with a second touch
		Proof proof = PROOF_NONE;
		QByteArray proofSecret;
		int keyLength = 32;
	};

	explicit Rekey(QObject *parent = nullptr);
	~Rekey() override;

	void start(const Request &request);
	// Call after backupKeyNeeded(), once the user has swapped keys.
	void continueWithBackupKey();
	void cancel();

	Step step() const { return m_step; }
	// Text for the touch prompt, describing which key to touch and why.
	QString stepDescription() const;
	LoginKeyBuilder *builder() { return m_builder; }

	// Results, valid after finished(STATUS_OK). take*() hands over the
	// only copy.
	QByteArray takeOldKey();
	QByteArray takeNewKey();
	QByteArray newHashfn() const { return m_newHashfn; }
	QByteArray newSalt() const { return m_newSalt; }

	// Makes a fresh random salt. Exposed for tests.
	static QByteArray randomSalt();

signals:
	void backupKeyNeeded();
	void finished(int status);

private slots:
	void builderFinished(int status);

private:
	void fail(int status);
	void clearSecrets();
	void finishNewKey();

	LoginKeyBuilder *m_builder;
	Request m_req;
	Step m_step;
	QByteArray m_oldKey;
	QByteArray m_newPasswordKey;
	QByteArray m_newKey;
	QByteArray m_newSalt;
	QByteArray m_newHashfn;
	QByteArray m_challenge;
	QByteArray m_response;
	quint32 m_serial;
	int m_padMode;
};

} // namespace HwKey

#endif
