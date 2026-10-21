#ifndef HWKEY_LOGINKEYBUILDER_H
#define HWKEY_LOGINKEYBUILDER_H

// Builds login keys in the background so the window stays responsive while
// scrypt runs and while the YubiKey waits for a touch.
//
// Typical use (unlocking):
//   builder->start(password, hashfn, salt, keyLength);
//   ... touchNeeded(slot) fires: show "touch your YubiKey" with Cancel
//   ... finished(status) fires: if status == HwKey::STATUS_OK, takeKey()
//
// Cancel() can be called at any time. The background work then finishes on
// its own, its results are wiped, and finished(STATUS_CANCELED) is emitted
// straight away.

#include <QObject>
#include <QThread>
#include <QAtomicInt>
#include <QByteArray>
#include <QString>

#include "yubikey.h"

namespace HwKey {

class LoginKeyJob : public QThread
{
	Q_OBJECT
public:
	enum Kind {
		FULL_LOGIN_KEY, // password (+ YubiKey or recovery secret) as the device asks
		PASSWORD_KEY,   // only the scrambled password
		TOUCH           // only ask the YubiKey for an answer
	};
	Kind kind = FULL_LOGIN_KEY;
	QString password;
	QByteArray hashfn;
	QByteArray salt;
	QByteArray recoverySecret;
	QByteArray challenge;
	int keyLength = 0;
	int slot = 0;

	QAtomicInt canceled;
	int status = STATUS_ERROR;
	QByteArray key;
	QByteArray response;
	quint32 serial = 0;

	~LoginKeyJob() override;
signals:
	void touchNeeded(int slot);
protected:
	void run() override;
private:
	Status touch(int slot, const QByteArray &challenge, QByteArray &response);
};

class LoginKeyBuilder : public QObject
{
	Q_OBJECT
public:
	explicit LoginKeyBuilder(QObject *parent = nullptr);
	~LoginKeyBuilder() override;

	// The login key for the device's current settings. If the device needs
	// a YubiKey, either the YubiKey is asked (recoverySecret empty) or the
	// answer is computed from the recovery secret.
	void start(const QString &password, const QByteArray &hashfn, const QByteArray &salt,
		   int keyLength, const QByteArray &recoverySecret = QByteArray());
	// Only the scrambled password (set-up uses this with new settings).
	void startPasswordKey(const QString &password, const QByteArray &hashfn,
			      const QByteArray &salt, int keyLength);
	// Only a YubiKey answer for this challenge.
	void startTouch(int slot, const QByteArray &challenge);

	void cancel();
	bool busy() const { return m_job != nullptr; }

	// Hand over the result; the builder keeps no copy.
	QByteArray takeKey();
	QByteArray takeResponse();
	// Serial number of the YubiKey that answered (0 if unknown).
	quint32 lastSerial() const { return m_serial; }

signals:
	void touchNeeded(int slot);
	void finished(int status);

private slots:
	void jobFinished();
	void jobTouchNeeded(int slot);

private:
	void launch(LoginKeyJob *job);
	void dropJob();
	LoginKeyJob *m_job;
	QByteArray m_key;
	QByteArray m_response;
	quint32 m_serial;
};

} // namespace HwKey

#endif
