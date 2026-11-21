#include "changemasterpassword.h"

extern "C" {
#include "signetdev/host/signetdev.h"
};

#include <QPushButton>
#include <QLineEdit>
#include <QBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include "buttonwaitdialog.h"
#include "signetapplication.h"


#include "style.h"
#include "touchprompt.h"
#include "hwkey/loginkey.h"
#include "hwkey/rekey.h"

ChangeMasterPassword::ChangeMasterPassword(QWidget *parent) :
	QDialog(parent),
	m_newPasswordWarningMessage(nullptr),
    m_oldPasswordWarningMessage(nullptr),
	m_oldPasswordEdit(nullptr),
	m_newPasswordEdit(nullptr),
	m_newPasswordRepeatEdit(nullptr),
	m_buttonDialog(nullptr),
	m_signetdevCmdToken(-1)
{
	m_rekey = new HwKey::Rekey(this);
	connect(m_rekey, SIGNAL(finished(int)), this, SLOT(keysReady(int)));
	HwKey::Rekey *rekey = m_rekey;
	TouchPrompt::attach(m_rekey->builder(), this, "Change Master Password",
			    [rekey]() { return rekey->stepDescription(); });

	SignetApplication *app = SignetApplication::get();

	connect(app, SIGNAL(signetdevCmdResp(signetdevCmdRespInfo)), this,
		SLOT(signetdevCmdResp(signetdevCmdRespInfo)));

	setWindowModality(Qt::WindowModal);

	setWindowTitle("Change Master Password");
	QBoxLayout *layout = new QBoxLayout(QBoxLayout::TopToBottom);

	QBoxLayout *old_password_layout = new QBoxLayout(QBoxLayout::LeftToRight);
	m_oldPasswordEdit = new QLineEdit();
	m_oldPasswordEdit->setEchoMode(QLineEdit::Password);
	old_password_layout->addWidget(new genericText("Old password"));
	old_password_layout->addWidget(m_oldPasswordEdit);

	m_generatingKeys = new processingText("Generating login keys...");
	m_generatingKeys->hide();

	m_oldPasswordWarningMessage = new errorText("");
	m_oldPasswordWarningMessage->hide();

	QBoxLayout *new_password_layout = new QBoxLayout(QBoxLayout::LeftToRight);
	m_newPasswordEdit = new QLineEdit();
	m_newPasswordEdit->setEchoMode(QLineEdit::Password);
	new_password_layout->addWidget(new genericText("New password"));
	new_password_layout->addWidget(m_newPasswordEdit);

	QBoxLayout *new_password_repeat_layout = new QBoxLayout(QBoxLayout::LeftToRight);
	m_newPasswordRepeatEdit = new QLineEdit();
	m_newPasswordRepeatEdit->setEchoMode(QLineEdit::Password);
	new_password_repeat_layout->addWidget(new genericText("New password (repeat) "));
	new_password_repeat_layout->addWidget(m_newPasswordRepeatEdit);

	m_newPasswordWarningMessage = new errorText("");
	m_newPasswordWarningMessage->hide();

	m_changePasswordBtn = new QPushButton("Change password");
	m_changePasswordBtn->setAutoDefault(true);

	m_authSecurityLevel = new QSpinBox();
	m_authSecurityLevel->setRange(1, 8);
	m_authSecurityLevel->setValue(4);

	auto securityLevelEdit = new QHBoxLayout();
	securityLevelEdit->addWidget(new genericText("New security level (1-8)"));
	securityLevelEdit->addWidget(m_authSecurityLevel);

	m_securityLevelComment = new noteText("Note: The security level controls how long it takes to unlock your device. A value of (4) will secure your data against for most threats and allow you to unlock your device in 1-2 seconds. A value of (8) could increase login times to over a minute.");

	layout->addLayout(old_password_layout);
	layout->addWidget(m_oldPasswordWarningMessage);
	layout->addLayout(new_password_layout);
	layout->addLayout(new_password_repeat_layout);
	layout->addWidget(m_newPasswordWarningMessage);
	layout->addLayout(securityLevelEdit);
	layout->addWidget(m_securityLevelComment);
	{
		HwKey::ScryptParams scrypt;
		HwKey::Config cfg;
		if (HwKey::parseHashfn(SignetApplication::get()->getHashfn(), scrypt, cfg) && cfg.required) {
			layout->addWidget(new noteText(QString("Your YubiKey (slot %1) stays switched on. You'll touch it "
				"twice: once to confirm the old password, once to lock the vault with the new one.")
				.arg(cfg.slot)));
		}
	}
	layout->addWidget(m_generatingKeys);
	layout->addWidget(m_changePasswordBtn);
	setLayout(layout);

	connect(m_changePasswordBtn, SIGNAL(pressed()), this, SLOT(changePasswordUi()));
	connect(m_newPasswordEdit, SIGNAL(textEdited(QString)), this, SLOT(newPasswordTextEdited(QString)));
	connect(m_newPasswordRepeatEdit, SIGNAL(textEdited(QString)), this, SLOT(newPasswordTextEdited(QString)));
	connect(m_oldPasswordEdit, SIGNAL(textEdited(QString)), this, SLOT(oldPasswordTextEdited(QString)));
}

void ChangeMasterPassword::setInputsEnabled(bool enabled)
{
	m_newPasswordEdit->setEnabled(enabled);
	m_newPasswordRepeatEdit->setEnabled(enabled);
	m_oldPasswordEdit->setEnabled(enabled);
	m_changePasswordBtn->setEnabled(enabled);
	m_authSecurityLevel->setEnabled(enabled);
}

void ChangeMasterPassword::keysReady(int status)
{
	m_generatingKeys->hide();
	if (status != HwKey::STATUS_OK) {
		setInputsEnabled(true);
		m_securityLevelComment->show();
		if (status != HwKey::STATUS_CANCELED) {
			SignetApplication::messageBoxError(
				status == HwKey::STATUS_BAD_SETTINGS ? QMessageBox::Critical : QMessageBox::Warning,
				"Change Master Password", HwKey::statusMessage((HwKey::Status)status), this);
		}
		return;
	}
	QByteArray oldKey = m_rekey->takeOldKey();
	QByteArray newKey = m_rekey->takeNewKey();
	m_newHashfn = m_rekey->newHashfn();
	m_newSalt = m_rekey->newSalt();
	m_buttonDialog = new ButtonWaitDialog("Change Master Password", "change master password", this, true);
	connect(m_buttonDialog, SIGNAL(finished(int)), this, SLOT(changePasswordFinished(int)));
	m_buttonDialog->show();
	::signetdev_change_master_password(NULL, &m_signetdevCmdToken,
					   (u8 *)oldKey.data(), oldKey.length(),
					   (u8 *)newKey.data(), newKey.length(),
					   (u8 *)m_newHashfn.data(), m_newHashfn.length(),
					   (u8 *)m_newSalt.data(), m_newSalt.length());
	// The command keeps its own copy.
	HwKey::wipe(oldKey);
	HwKey::wipe(newKey);
}

ChangeMasterPassword::~ChangeMasterPassword()
{
	m_rekey->cancel();
}

void ChangeMasterPassword::newPasswordTextEdited(QString s)
{
	Q_UNUSED(s);
	m_newPasswordWarningMessage->hide();
}

void ChangeMasterPassword::oldPasswordTextEdited(QString s)
{
	Q_UNUSED(s);
	m_oldPasswordWarningMessage->hide();
}

void ChangeMasterPassword::changePasswordUi()
{
	if (m_newPasswordEdit->text() != m_newPasswordRepeatEdit->text()) {
		m_newPasswordWarningMessage->setText("New passwords don't match, try again");
		m_newPasswordWarningMessage->show();
		return;
	}
	SignetApplication *app = SignetApplication::get();
	HwKey::ScryptParams oldScrypt;
	HwKey::Config cfg;
	if (!HwKey::parseHashfn(app->getHashfn(), oldScrypt, cfg) &&
	    !(app->getHashfn().size() > 0 && app->getHashfn().at(0) == HwKey::HASHFN_SCRYPT_LEGACY)) {
		SignetApplication::messageBoxError(QMessageBox::Critical, "Change Master Password",
			HwKey::statusMessage(HwKey::STATUS_BAD_SETTINGS), this);
		return;
	}

	HwKey::Rekey::Request req;
	req.oldPassword = m_oldPasswordEdit->text();
	req.oldHashfn = app->getHashfn();
	req.oldSalt = app->getSalt();
	req.newPassword = m_newPasswordEdit->text();
	req.newScrypt.log2N = 11 + m_authSecurityLevel->value();
	req.newScrypt.r = 8;
	req.newScrypt.p = 1;
	// Keep the YubiKey exactly as it is: same slot, same padding, so the
	// recovery secret or backup key keeps working.
	req.newRequired = cfg.required;
	req.newSlot = cfg.slot;
	req.knownPadMode = cfg.padMode;
	req.proof = HwKey::Rekey::PROOF_NONE;
	req.keyLength = app->getKeyLength();

	setInputsEnabled(false);
	m_generatingKeys->show();
	m_securityLevelComment->hide();
	m_rekey->start(req);
	HwKey::wipe(req.oldPassword);
	HwKey::wipe(req.newPassword);
}

void ChangeMasterPassword::changePasswordFinished(int code)
{
	if (code != QMessageBox::Ok) {
		m_securityLevelComment->show();
		::signetdev_cancel_button_wait();
		setInputsEnabled(true);
	}
}

void ChangeMasterPassword::signetdevCmdResp(signetdevCmdRespInfo info)
{
	int code = info.resp_code;

	if (m_signetdevCmdToken != info.token) {
		return;
	}
	m_signetdevCmdToken = -1;
	if (m_buttonDialog) {
		m_buttonDialog->done(QMessageBox::Ok);
		m_buttonDialog->deleteLater();
		m_buttonDialog = nullptr;
	}

	switch (code) {
	case OKAY: {
		QMessageBox *box = SignetApplication::messageBoxError(QMessageBox::Information, "Change Master Password", "Password change successful", this);;
		connect(box, SIGNAL(finished(int)), this, SLOT(close()));
		SignetApplication::get()->setHashfn(m_newHashfn);
		SignetApplication::get()->setSalt(m_newSalt);
		m_securityLevelComment->show();
	}
	break;
	case BAD_PASSWORD:
		m_oldPasswordWarningMessage->setText("Old password incorrect (or not the right YubiKey), try again");
		m_oldPasswordWarningMessage->show();
		setInputsEnabled(true);
		m_securityLevelComment->show();
		break;
	case BUTTON_PRESS_TIMEOUT:
	case BUTTON_PRESS_CANCELED:
		setInputsEnabled(true);
		m_securityLevelComment->show();
		break;
	case SIGNET_ERROR_DISCONNECT:
	case SIGNET_ERROR_QUIT:
		close();
		break;
	default: {
		emit abort();
		close();
	}
	break;
	}
}
