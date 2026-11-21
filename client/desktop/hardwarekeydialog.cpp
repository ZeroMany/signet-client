#include "hardwarekeydialog.h"

extern "C" {
#include "signetdev/host/signetdev.h"
};

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>

#include "buttonwaitdialog.h"
#include "style.h"
#include "touchprompt.h"
#include "hwkey/loginkey.h"
#include "hwkey/rekey.h"

namespace {
const char TITLE[] = "YubiKey";

QLineEdit *secretEdit()
{
	QLineEdit *e = new QLineEdit();
	e->setEchoMode(QLineEdit::Password);
	e->setPlaceholderText("40 characters, 0-9 and a-f");
	return e;
}
}

HardwareKeyDialog::HardwareKeyDialog(QWidget *parent) :
	QDialog(parent),
	m_enabledNow(false),
	m_slotNow(0),
	m_buttonDialog(nullptr),
	m_signetdevCmdToken(-1),
	m_pendingAction(ACTION_ENABLE)
{
	setWindowModality(Qt::WindowModal);
	setWindowTitle(TITLE);

	HwKey::ScryptParams scrypt;
	HwKey::Config cfg;
	HwKey::parseHashfn(SignetApplication::get()->getHashfn(), scrypt, cfg);
	m_enabledNow = cfg.required;
	m_slotNow = cfg.slot;

	m_rekey = new HwKey::Rekey(this);
	connect(m_rekey, SIGNAL(finished(int)), this, SLOT(keysReady(int)));
	connect(m_rekey, SIGNAL(backupKeyNeeded()), this, SLOT(backupKeyNeeded()));
	HwKey::Rekey *rekey = m_rekey;
	TouchPrompt::attach(m_rekey->builder(), this, TITLE,
			    [rekey]() { return rekey->stepDescription(); });
	connect(SignetApplication::get(), SIGNAL(signetdevCmdResp(signetdevCmdRespInfo)),
		this, SLOT(signetdevCmdResp(signetdevCmdRespInfo)));

	QBoxLayout *layout = new QBoxLayout(QBoxLayout::TopToBottom);

	m_status = new genericText(m_enabledNow ?
		QString("A YubiKey is ON for this Signet (slot %1). Unlocking needs your master password "
			"and a touch of the key.").arg(m_slotNow) :
		QString("A YubiKey is OFF. Unlocking needs only your master password."));
	m_status->setWordWrap(true);
	layout->addWidget(m_status);

	QLabel *about = new noteText(
		"With a YubiKey switched on, your Signet can only be unlocked with your master password "
		"AND a touch of your YubiKey. Everything happens on this computer, with no internet. "
		"Works with YubiKey 5 series and YubiKey NEO (not the blue 'Security Key' or YubiKey Bio).");
	about->setWordWrap(true);
	layout->addWidget(about);

	QBoxLayout *pw = new QBoxLayout(QBoxLayout::LeftToRight);
	m_password = new QLineEdit();
	m_password->setEchoMode(QLineEdit::Password);
	pw->addWidget(new genericText("Master password"));
	pw->addWidget(m_password);
	layout->addLayout(pw);

	// Proving the current YubiKey, when one is on.
	m_currentBox = new QGroupBox("Your current YubiKey");
	QBoxLayout *cur = new QBoxLayout(QBoxLayout::TopToBottom);
	cur->addWidget(new noteText("You'll be asked to touch it."));
	m_useRecovery = new QCheckBox("I don't have it: use its recovery secret instead");
	m_currentRecovery = secretEdit();
	cur->addWidget(m_useRecovery);
	cur->addWidget(m_currentRecovery);
	m_currentBox->setLayout(cur);
	layout->addWidget(m_currentBox);

	m_actionBox = new QGroupBox("What do you want to do?");
	QBoxLayout *act = new QBoxLayout(QBoxLayout::TopToBottom);
	m_replace = new QRadioButton("Switch to a different YubiKey or slot");
	m_disable = new QRadioButton("Turn the YubiKey off (password only)");
	m_replace->setChecked(true);
	act->addWidget(m_replace);
	act->addWidget(m_disable);
	m_actionBox->setLayout(act);
	layout->addWidget(m_actionBox);

	// Setting up a (new) YubiKey.
	m_newBox = new QGroupBox(m_enabledNow ? "The YubiKey to use from now on" : "Set up your YubiKey");
	QBoxLayout *nb = new QBoxLayout(QBoxLayout::TopToBottom);
	QLabel *howto = new noteText(
		"First, set up a challenge-response slot with Yubico's free YubiKey Manager: Applications, "
		"OTP, pick a slot, Configure, Challenge-response. Click Generate, WRITE DOWN the secret key "
		"shown, and tick 'Require touch'. (Command line: ykman otp chalresp --touch --generate 2.) "
		"A slot already used for KeePassXC works too.");
	howto->setWordWrap(true);
	nb->addWidget(howto);
	QBoxLayout *slotRow = new QBoxLayout(QBoxLayout::LeftToRight);
	m_slot = new QComboBox();
	m_slot->addItem("Slot 2 (long touch)", 2);
	m_slot->addItem("Slot 1 (short touch)", 1);
	slotRow->addWidget(new genericText("Slot"));
	slotRow->addWidget(m_slot);
	nb->addLayout(slotRow);
	QLabel *recoveryNote = new noteText(
		"If this YubiKey is lost or breaks, you will need its secret or a backup YubiKey to get "
		"into your vault. Show the app you have one before it switches on:");
	recoveryNote->setWordWrap(true);
	nb->addWidget(recoveryNote);
	m_proofSecret = new QRadioButton("I wrote down the slot's secret. Here it is:");
	m_proofSecretEdit = secretEdit();
	m_proofBackup = new QRadioButton("I have a backup YubiKey programmed with the same secret");
	m_proofSecret->setChecked(true);
	nb->addWidget(m_proofSecret);
	nb->addWidget(m_proofSecretEdit);
	nb->addWidget(m_proofBackup);
	m_newBox->setLayout(nb);
	layout->addWidget(m_newBox);

	m_working = new processingText("Working...");
	m_working->hide();
	layout->addWidget(m_working);

	QBoxLayout *buttons = new QBoxLayout(QBoxLayout::LeftToRight);
	m_applyButton = new QPushButton(m_enabledNow ? "Apply" : "Turn on");
	QPushButton *close = new QPushButton("Close");
	buttons->addWidget(m_applyButton);
	buttons->addWidget(close);
	layout->addLayout(buttons);
	setLayout(layout);

	connect(m_applyButton, SIGNAL(clicked()), this, SLOT(apply()));
	connect(close, SIGNAL(clicked()), this, SLOT(close()));
	connect(m_useRecovery, SIGNAL(toggled(bool)), this, SLOT(updateVisibility()));
	connect(m_replace, SIGNAL(toggled(bool)), this, SLOT(updateVisibility()));
	connect(m_disable, SIGNAL(toggled(bool)), this, SLOT(updateVisibility()));
	connect(m_proofSecret, SIGNAL(toggled(bool)), this, SLOT(updateVisibility()));
	updateVisibility();
}

HardwareKeyDialog::~HardwareKeyDialog()
{
	m_rekey->cancel();
}

HardwareKeyDialog::Action HardwareKeyDialog::currentAction() const
{
	if (!m_enabledNow) {
		return ACTION_ENABLE;
	}
	return m_disable->isChecked() ? ACTION_DISABLE : ACTION_REPLACE;
}

void HardwareKeyDialog::updateVisibility()
{
	m_currentBox->setVisible(m_enabledNow);
	m_currentRecovery->setVisible(m_useRecovery->isChecked());
	m_actionBox->setVisible(m_enabledNow);
	m_newBox->setVisible(currentAction() != ACTION_DISABLE);
	m_proofSecretEdit->setEnabled(m_proofSecret->isChecked());
	if (m_enabledNow) {
		m_applyButton->setText(currentAction() == ACTION_DISABLE ? "Turn off" : "Switch key");
	}
	adjustSize();
}

void HardwareKeyDialog::setBusy(bool busy)
{
	m_working->setVisible(busy);
	m_applyButton->setEnabled(!busy);
	m_password->setEnabled(!busy);
	m_currentBox->setEnabled(!busy);
	m_actionBox->setEnabled(!busy);
	m_newBox->setEnabled(!busy);
}

void HardwareKeyDialog::showError(int status)
{
	if (status == HwKey::STATUS_CANCELED) {
		return;
	}
	int slot = currentAction() == ACTION_DISABLE ? m_slotNow : m_slot->currentData().toInt();
	SignetApplication::messageBoxError(
		status == HwKey::STATUS_BAD_SETTINGS ? QMessageBox::Critical : QMessageBox::Warning,
		TITLE, HwKey::statusMessage((HwKey::Status)status, slot), this);
}

void HardwareKeyDialog::apply()
{
	SignetApplication *app = SignetApplication::get();
	Action action = currentAction();

	HwKey::ScryptParams scrypt;
	HwKey::Config cfg;
	QByteArray hashfn = app->getHashfn();
	bool legacy = !hashfn.isEmpty() && hashfn.at(0) == HwKey::HASHFN_SCRYPT_LEGACY;
	if (!HwKey::parseHashfn(hashfn, scrypt, cfg)) {
		if (!legacy) {
			showError(HwKey::STATUS_BAD_SETTINGS);
			return;
		}
		// Very old settings: move to today's default work level.
		scrypt.log2N = 15;
		scrypt.r = 8;
		scrypt.p = 1;
	}

	if (m_password->text().isEmpty()) {
		SignetApplication::messageBoxError(QMessageBox::Warning, TITLE, "Enter your master password.", this);
		return;
	}

	HwKey::Rekey::Request req;
	req.oldPassword = m_password->text();
	req.newPassword = m_password->text();
	req.oldHashfn = hashfn;
	req.oldSalt = app->getSalt();
	req.newScrypt = scrypt;
	req.keyLength = app->getKeyLength();

	if (m_enabledNow && m_useRecovery->isChecked()) {
		QString text = m_currentRecovery->text();
		bool ok = HwKey::parseSecret(text, req.oldRecoverySecret);
		HwKey::wipe(text);
		if (!ok) {
			showError(HwKey::STATUS_BAD_SECRET);
			return;
		}
	}

	if (action == ACTION_DISABLE) {
		QMessageBox::StandardButton b = QMessageBox::warning(this, TITLE,
			"Turn the YubiKey off?\n\nAfter this, anyone with your master password and your Signet "
			"can unlock it, without the YubiKey.",
			QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (b != QMessageBox::Yes) {
			HwKey::wipe(req.oldPassword);
			HwKey::wipe(req.newPassword);
			HwKey::wipe(req.oldRecoverySecret);
			return;
		}
		req.newRequired = false;
	} else {
		req.newRequired = true;
		req.newSlot = m_slot->currentData().toInt();
		req.knownPadMode = HwKey::PAD_UNKNOWN; // learn it from the key
		if (m_proofSecret->isChecked()) {
			QString text = m_proofSecretEdit->text();
			bool ok = HwKey::parseSecret(text, req.proofSecret);
			HwKey::wipe(text);
			if (!ok) {
				HwKey::wipe(req.oldRecoverySecret);
				showError(HwKey::STATUS_BAD_SECRET);
				return;
			}
			req.proof = HwKey::Rekey::PROOF_SECRET;
		} else {
			req.proof = HwKey::Rekey::PROOF_BACKUP_KEY;
		}
	}

	m_pendingAction = action;
	setBusy(true);
	m_rekey->start(req);
	HwKey::wipe(req.oldPassword);
	HwKey::wipe(req.newPassword);
	HwKey::wipe(req.oldRecoverySecret);
	HwKey::wipe(req.proofSecret);
}

void HardwareKeyDialog::backupKeyNeeded()
{
	QMessageBox::StandardButton b = QMessageBox::information(this, TITLE,
		"Your main YubiKey worked.\n\nNow unplug it, plug in your BACKUP YubiKey, and press OK. "
		"You'll be asked to touch the backup key.",
		QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
	if (b == QMessageBox::Ok) {
		m_rekey->continueWithBackupKey();
	} else {
		m_rekey->cancel();
	}
}

void HardwareKeyDialog::keysReady(int status)
{
	if (status != HwKey::STATUS_OK) {
		setBusy(false);
		showError(status);
		return;
	}
	QByteArray oldKey = m_rekey->takeOldKey();
	QByteArray newKey = m_rekey->takeNewKey();
	m_newHashfn = m_rekey->newHashfn();
	m_newSalt = m_rekey->newSalt();
	m_buttonDialog = new ButtonWaitDialog(TITLE,
		m_pendingAction == ACTION_DISABLE ? "turn the YubiKey off" : "switch the YubiKey on",
		this, true);
	connect(m_buttonDialog, SIGNAL(finished(int)), this, SLOT(buttonDialogFinished(int)));
	m_buttonDialog->show();
	::signetdev_change_master_password(NULL, &m_signetdevCmdToken,
					   (u8 *)oldKey.data(), oldKey.length(),
					   (u8 *)newKey.data(), newKey.length(),
					   (u8 *)m_newHashfn.data(), m_newHashfn.length(),
					   (u8 *)m_newSalt.data(), m_newSalt.length());
	HwKey::wipe(oldKey);
	HwKey::wipe(newKey);
}

void HardwareKeyDialog::buttonDialogFinished(int code)
{
	if (code != QMessageBox::Ok) {
		::signetdev_cancel_button_wait();
		setBusy(false);
	}
}

void HardwareKeyDialog::signetdevCmdResp(signetdevCmdRespInfo info)
{
	if (m_signetdevCmdToken != info.token) {
		return;
	}
	m_signetdevCmdToken = -1;
	if (m_buttonDialog) {
		m_buttonDialog->done(QMessageBox::Ok);
		m_buttonDialog->deleteLater();
		m_buttonDialog = nullptr;
	}
	setBusy(false);

	switch (info.resp_code) {
	case OKAY: {
		SignetApplication::get()->setHashfn(m_newHashfn);
		SignetApplication::get()->setSalt(m_newSalt);
		QString msg = m_pendingAction == ACTION_DISABLE ?
			QString("The YubiKey is now off. Your Signet unlocks with the master password alone.") :
			QString("Done. From now on your Signet needs your master password and a touch of your "
				"YubiKey to unlock.\n\nImportant: make a new backup now (Device, Backup to file). "
				"Backups made before today can still be opened with the password alone, so store "
				"or delete them with that in mind.\n\nKeep the slot's secret or your backup "
				"YubiKey somewhere safe and separate from your Signet.");
		QMessageBox *box = SignetApplication::messageBoxError(QMessageBox::Information, TITLE, msg, this);
		connect(box, SIGNAL(finished(int)), this, SLOT(close()));
	} break;
	case BAD_PASSWORD:
		SignetApplication::messageBoxError(QMessageBox::Warning, TITLE,
			"Incorrect master password, or not the right YubiKey or recovery secret. Nothing was changed.", this);
		break;
	case BUTTON_PRESS_TIMEOUT:
	case BUTTON_PRESS_CANCELED:
		SignetApplication::messageBoxError(QMessageBox::Information, TITLE,
			"The Signet's button wasn't pressed, so nothing was changed.", this);
		break;
	case SIGNET_ERROR_DISCONNECT:
	case SIGNET_ERROR_QUIT:
		close();
		break;
	default:
		emit abort();
		close();
		break;
	}
}
