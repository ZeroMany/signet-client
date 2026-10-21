#include "loginkeybuilder.h"
#include "loginkey.h"

#include <QElapsedTimer>

namespace HwKey {

LoginKeyJob::~LoginKeyJob()
{
	wipe(password);
	wipe(recoverySecret);
	wipe(key);
	wipe(response);
}

Status LoginKeyJob::touch(int slot, const QByteArray &challenge, QByteArray &response)
{
	emit touchNeeded(slot);
	QElapsedTimer timer;
	timer.start();
	Status s = responder()->challengeResponse(slot, challenge, response, &serial);
	if (s == STATUS_OK && timer.elapsed() < minimumTouchMs()) {
		wipe(response);
		return STATUS_TOUCH_NOT_REQUIRED;
	}
	if (s != STATUS_OK) {
		wipe(response);
	}
	return s;
}

void LoginKeyJob::run()
{
	status = STATUS_ERROR;

	if (kind == TOUCH) {
		status = touch(slot, challenge, response);
		return;
	}

	bool legacy = !hashfn.isEmpty() && hashfn.at(0) == HASHFN_SCRYPT_LEGACY;
	ScryptParams scrypt;
	Config cfg;
	if (!legacy && !parseHashfn(hashfn, scrypt, cfg)) {
		wipe(password);
		status = STATUS_BAD_SETTINGS;
		return;
	}

	QByteArray pwKey;
	passwordKey(password, hashfn, salt, keyLength, pwKey);
	wipe(password);
	if (pwKey.isEmpty()) {
		status = STATUS_BAD_SETTINGS;
		return;
	}

	if (kind == PASSWORD_KEY || !cfg.required) {
		key = pwKey;
		pwKey.clear(); // `key` now holds the only copy
		status = STATUS_OK;
		return;
	}

	if (canceled.loadAcquire()) {
		wipe(pwKey);
		status = STATUS_CANCELED;
		return;
	}

	QByteArray ch = challengeForSalt(salt);
	QByteArray resp;
	Status s;
	if (!recoverySecret.isEmpty()) {
		resp = softwareResponse(recoverySecret, ch, cfg.padMode);
		s = resp.size() == RESPONSE_SIZE ? STATUS_OK : STATUS_BAD_SECRET;
	} else {
		s = touch(cfg.slot, ch, resp);
	}
	wipe(recoverySecret);
	if (s == STATUS_OK) {
		key = combine(pwKey, resp, keyLength);
		if (key.isEmpty()) {
			s = STATUS_ERROR;
		}
	}
	wipe(pwKey);
	wipe(resp);
	status = s;
}

LoginKeyBuilder::LoginKeyBuilder(QObject *parent) :
	QObject(parent),
	m_job(nullptr),
	m_serial(0)
{
}

LoginKeyBuilder::~LoginKeyBuilder()
{
	dropJob();
	wipe(m_key);
	wipe(m_response);
}

void LoginKeyBuilder::launch(LoginKeyJob *job)
{
	dropJob();
	wipe(m_key);
	wipe(m_response);
	m_serial = 0;
	m_job = job;
	connect(job, SIGNAL(touchNeeded(int)), this, SLOT(jobTouchNeeded(int)), Qt::QueuedConnection);
	connect(job, SIGNAL(finished()), this, SLOT(jobFinished()), Qt::QueuedConnection);
	// The job always cleans itself up, even if this builder is gone.
	connect(job, SIGNAL(finished()), job, SLOT(deleteLater()));
	job->start();
}

void LoginKeyBuilder::dropJob()
{
	if (m_job) {
		m_job->canceled.storeRelease(1);
		disconnect(m_job, nullptr, this, nullptr);
		m_job = nullptr;
	}
}

void LoginKeyBuilder::start(const QString &password, const QByteArray &hashfn, const QByteArray &salt,
			    int keyLength, const QByteArray &recoverySecret)
{
	LoginKeyJob *job = new LoginKeyJob();
	job->kind = LoginKeyJob::FULL_LOGIN_KEY;
	job->password = password;
	job->hashfn = hashfn;
	job->salt = salt;
	job->keyLength = keyLength;
	job->recoverySecret = recoverySecret;
	launch(job);
}

void LoginKeyBuilder::startPasswordKey(const QString &password, const QByteArray &hashfn,
				       const QByteArray &salt, int keyLength)
{
	LoginKeyJob *job = new LoginKeyJob();
	job->kind = LoginKeyJob::PASSWORD_KEY;
	job->password = password;
	job->hashfn = hashfn;
	job->salt = salt;
	job->keyLength = keyLength;
	launch(job);
}

void LoginKeyBuilder::startTouch(int slot, const QByteArray &challenge)
{
	LoginKeyJob *job = new LoginKeyJob();
	job->kind = LoginKeyJob::TOUCH;
	job->slot = slot;
	job->challenge = challenge;
	launch(job);
}

void LoginKeyBuilder::cancel()
{
	if (m_job) {
		dropJob();
		emit finished(STATUS_CANCELED);
	}
}

void LoginKeyBuilder::jobTouchNeeded(int slot)
{
	// Ignore a late message from a job that was canceled.
	if (sender() == m_job) {
		emit touchNeeded(slot);
	}
}

void LoginKeyBuilder::jobFinished()
{
	LoginKeyJob *job = qobject_cast<LoginKeyJob *>(sender());
	if (!job || job != m_job) {
		return;
	}
	m_job = nullptr;
	int status = job->status;
	if (status == STATUS_OK) {
		m_key = job->key;
		m_response = job->response;
		m_serial = job->serial;
	}
	wipe(job->key);
	wipe(job->response);
	emit finished(status);
}

QByteArray LoginKeyBuilder::takeKey()
{
	QByteArray k = m_key;
	m_key = QByteArray();
	return k;
}

QByteArray LoginKeyBuilder::takeResponse()
{
	QByteArray r = m_response;
	m_response = QByteArray();
	return r;
}

} // namespace HwKey
