#include "rekey.h"

#include <gcrypt.h>

namespace HwKey {

Rekey::Rekey(QObject *parent) :
	QObject(parent),
	m_builder(new LoginKeyBuilder(this)),
	m_step(STEP_NONE),
	m_serial(0),
	m_padMode(PAD_UNKNOWN)
{
	connect(m_builder, SIGNAL(finished(int)), this, SLOT(builderFinished(int)));
}

Rekey::~Rekey()
{
	m_step = STEP_NONE;
	m_builder->cancel();
	clearSecrets();
	wipe(m_oldKey);
	wipe(m_newKey);
}

QByteArray Rekey::randomSalt()
{
	QByteArray salt(32, 0);
	gcry_randomize(salt.data(), (size_t)salt.size(), GCRY_STRONG_RANDOM);
	return salt;
}

void Rekey::clearSecrets()
{
	wipe(m_req.oldPassword);
	wipe(m_req.newPassword);
	wipe(m_req.oldRecoverySecret);
	wipe(m_req.proofSecret);
	wipe(m_newPasswordKey);
	wipe(m_response);
}

void Rekey::fail(int status)
{
	// Mark as stopped first, so the builder's own "canceled" is ignored.
	m_step = STEP_NONE;
	m_builder->cancel();
	clearSecrets();
	wipe(m_oldKey);
	wipe(m_newKey);
	emit finished(status);
}

void Rekey::start(const Request &request)
{
	m_step = STEP_NONE;
	m_builder->cancel();
	clearSecrets();
	wipe(m_oldKey);
	wipe(m_newKey);
	m_req = request;
	m_serial = 0;
	m_padMode = request.knownPadMode;
	if (request.newRequired && request.newSlot != 1 && request.newSlot != 2) {
		fail(STATUS_ERROR);
		return;
	}
	if (request.newRequired && request.proof == PROOF_NONE && request.knownPadMode == PAD_UNKNOWN) {
		// A brand-new key must come with a way to recover.
		fail(STATUS_ERROR);
		return;
	}
	m_newSalt = randomSalt();
	m_step = STEP_CURRENT_LOGIN;
	m_builder->start(m_req.oldPassword, m_req.oldHashfn, m_req.oldSalt,
			 m_req.keyLength, m_req.oldRecoverySecret);
	wipe(m_req.oldPassword);
	wipe(m_req.oldRecoverySecret);
}

void Rekey::cancel()
{
	if (m_step != STEP_NONE && m_step != STEP_DONE) {
		fail(STATUS_CANCELED);
	}
}

void Rekey::continueWithBackupKey()
{
	if (m_step != STEP_BACKUP_KEY) {
		return;
	}
	m_builder->startTouch(m_req.newSlot, m_challenge);
}

QString Rekey::stepDescription() const
{
	switch (m_step) {
	case STEP_CURRENT_LOGIN:
		return "This first touch confirms the YubiKey you use now.";
	case STEP_NEW_KEY:
		return "This touch sets up the YubiKey you want to use from now on.";
	case STEP_NEW_KEY_AGAIN:
		return "Touch the same key once more, so the app can learn how its slot reads challenges.";
	case STEP_BACKUP_KEY:
		return "Touch your BACKUP YubiKey now, to check it holds the same secret.";
	default:
		return QString();
	}
}

void Rekey::builderFinished(int status)
{
	if (m_step == STEP_NONE || m_step == STEP_DONE) {
		return;
	}
	if (status != STATUS_OK) {
		fail(status);
		return;
	}

	switch (m_step) {
	case STEP_CURRENT_LOGIN: {
		m_oldKey = m_builder->takeKey();
		// Scramble the new password with the new salt. The scrypt part is
		// the same whether or not a YubiKey is used.
		Config none;
		QByteArray pwOnly = buildHashfn(m_req.newScrypt, none);
		m_step = STEP_NEW_PASSWORD;
		m_builder->startPasswordKey(m_req.newPassword, pwOnly, m_newSalt, m_req.keyLength);
		wipe(m_req.newPassword);
	} break;
	case STEP_NEW_PASSWORD:
		m_newPasswordKey = m_builder->takeKey();
		if (!m_req.newRequired) {
			m_newKey = m_newPasswordKey;
			m_newPasswordKey.clear();
			Config none;
			m_newHashfn = buildHashfn(m_req.newScrypt, none);
			m_step = STEP_DONE;
			clearSecrets();
			emit finished(STATUS_OK);
			return;
		}
		m_challenge = challengeForSalt(m_newSalt);
		m_step = STEP_NEW_KEY;
		m_builder->startTouch(m_req.newSlot, m_challenge);
		break;
	case STEP_NEW_KEY:
		m_response = m_builder->takeResponse();
		m_serial = m_builder->lastSerial();
		if (m_padMode == PAD_UNKNOWN) {
			// Ask again with different padding. If the answer is the same,
			// the slot ignores padding; otherwise it reads all 64 bytes.
			m_step = STEP_NEW_KEY_AGAIN;
			QByteArray other = challengeForSalt(m_newSalt, alternatePadByte(m_challenge));
			m_builder->startTouch(m_req.newSlot, other);
			return;
		}
		finishNewKey();
		break;
	case STEP_NEW_KEY_AGAIN: {
		QByteArray second = m_builder->takeResponse();
		m_padMode = constantTimeEqual(second, m_response) ? PAD_VARIABLE : PAD_FIXED64;
		wipe(second);
		finishNewKey();
	} break;
	case STEP_BACKUP_KEY: {
		QByteArray backup = m_builder->takeResponse();
		quint32 serial = m_builder->lastSerial();
		bool same = constantTimeEqual(backup, m_response);
		wipe(backup);
		if (serial != 0 && serial == m_serial) {
			fail(STATUS_SAME_KEY);
			return;
		}
		if (!same) {
			fail(STATUS_PROOF_MISMATCH);
			return;
		}
		m_req.proof = PROOF_NONE; // proven
		finishNewKey();
	} break;
	default:
		break;
	}
}

void Rekey::finishNewKey()
{
	if (m_req.proof == PROOF_SECRET) {
		QByteArray expected = softwareResponse(m_req.proofSecret, m_challenge, m_padMode);
		bool ok = constantTimeEqual(expected, m_response);
		wipe(expected);
		wipe(m_req.proofSecret);
		if (!ok) {
			fail(STATUS_PROOF_MISMATCH);
			return;
		}
	} else if (m_req.proof == PROOF_BACKUP_KEY) {
		m_step = STEP_BACKUP_KEY;
		emit backupKeyNeeded();
		return;
	}

	m_newKey = combine(m_newPasswordKey, m_response, m_req.keyLength);
	if (m_newKey.isEmpty()) {
		fail(STATUS_ERROR);
		return;
	}
	Config cfg;
	cfg.required = true;
	cfg.slot = m_req.newSlot;
	cfg.padMode = m_padMode;
	m_newHashfn = buildHashfn(m_req.newScrypt, cfg);
	m_step = STEP_DONE;
	clearSecrets();
	emit finished(STATUS_OK);
}

QByteArray Rekey::takeOldKey()
{
	QByteArray k = m_oldKey;
	m_oldKey = QByteArray();
	return k;
}

QByteArray Rekey::takeNewKey()
{
	QByteArray k = m_newKey;
	m_newKey = QByteArray();
	return k;
}

} // namespace HwKey
