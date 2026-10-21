#include "loginwindow.h"
#include <QtGui>
#include <QBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QLineEdit>

#include "buttonwaitdialog.h"
#include "signetapplication.h"
#include "touchprompt.h"
#include "hwkey/loginkey.h"
#include "hwkey/loginkeybuilder.h"

extern "C" {
#include "signetdev/host/signetdev.h"
};

#include "../desktop/mainwindow.h"

#include "buttonwaitwidget.h"
#include "generictext.h"
#include "errortext.h"
#include "notetext.h"
#include "processingtext.h"

LoginWindow::LoginWindow(QWidget *parent) : QWidget(parent),
	m_parent(static_cast<MainWindow *>(parent)),
	m_loggingIn(false),
	m_preparingLogin(false),
	m_hwKeyRequired(false),
	m_hwKeySlot(0),
	m_signetdevCmdToken(-1)
{
	m_keyBuilder = new HwKey::LoginKeyBuilder(this);
	TouchPrompt::attach(m_keyBuilder, this, "Unlock",
			    "After that, press the button on your Signet.");

	SignetApplication *app = SignetApplication::get();

	QObject::connect(m_keyBuilder, SIGNAL(finished(int)), this, SLOT(keyGenerated(int)));
	QObject::connect(app, SIGNAL(signetdevCmdResp(signetdevCmdRespInfo)),
			 this, SLOT(signetdevCmdResp(signetdevCmdRespInfo)));

	QBoxLayout *top_layout = new QBoxLayout(QBoxLayout::TopToBottom);
	QBoxLayout *layout = new QBoxLayout(QBoxLayout::LeftToRight);
	QLabel *password_label = new genericText("Password");

	m_incorrectPassword = new errorText("Incorrect password");
	m_incorrectPassword->hide();
	m_passwordInput = new QLineEdit();
	m_passwordInput->setEchoMode(QLineEdit::Password);
	m_loginButton = new QPushButton("Unlock");
	m_loginButton->setAutoDefault(true);

	m_preparingLabel = new processingText("Generating authorization key...");
	m_preparingLabel->hide();

	// Shown only when this Signet needs a YubiKey too.
	m_hwKeyNote = new noteText("");
	m_hwKeyNote->setWordWrap(true);
	m_hwKeyNote->hide();
	m_useRecovery = new QCheckBox("I don't have my YubiKey: use my recovery secret instead");
	m_useRecovery->hide();
	m_recoveryLabel = new genericText("Recovery secret");
	m_recoveryLabel->hide();
	m_recoveryInput = new QLineEdit();
	m_recoveryInput->setEchoMode(QLineEdit::Password);
	m_recoveryInput->setPlaceholderText("40 characters, 0-9 and a-f");
	m_recoveryInput->hide();
	QBoxLayout *recovery_layout = new QBoxLayout(QBoxLayout::LeftToRight);
	recovery_layout->addWidget(m_recoveryLabel);
	recovery_layout->addWidget(m_recoveryInput);

	layout->addWidget(password_label);
	layout->addWidget(m_passwordInput);
	top_layout->addWidget(new genericText("Enter your master password"));
	top_layout->addItem(layout);
	top_layout->addWidget(m_hwKeyNote);
	top_layout->addWidget(m_useRecovery);
	top_layout->addItem(recovery_layout);
	top_layout->addWidget(m_incorrectPassword);
	top_layout->addWidget(m_preparingLabel);
	top_layout->addWidget(m_loginButton);
	connect(m_loginButton, SIGNAL(pressed(void)),
		this, SLOT(doLogin(void)));
	connect(m_passwordInput, SIGNAL(returnPressed(void)),
		this, SLOT(doLogin(void)));
	connect(m_recoveryInput, SIGNAL(returnPressed(void)),
		this, SLOT(doLogin(void)));
	connect(m_passwordInput, SIGNAL(textEdited(QString)), this, SLOT(passwordTextEdited(QString)));
	connect(m_recoveryInput, SIGNAL(textEdited(QString)), this, SLOT(passwordTextEdited(QString)));
	connect(m_useRecovery, SIGNAL(toggled(bool)), this, SLOT(useRecoveryToggled(bool)));
	top_layout->setAlignment(Qt::AlignTop);
	setLayout(top_layout);
}

LoginWindow::~LoginWindow()
{
	m_preparingLogin = false;
	m_loggingIn = false;
	m_keyBuilder->cancel();
}

void LoginWindow::updateHardwareKeyInfo()
{
	HwKey::ScryptParams scrypt;
	HwKey::Config cfg;
	QByteArray hashfn = SignetApplication::get()->getHashfn();
	// If the settings are damaged, the unlock attempt reports it.
	HwKey::parseHashfn(hashfn, scrypt, cfg);
	m_hwKeyRequired = cfg.required || (!hashfn.isEmpty() &&
					   (unsigned char)hashfn.at(0) == HwKey::HASHFN_SCRYPT_YUBIKEY);
	m_hwKeySlot = cfg.slot;
	if (m_hwKeyRequired) {
		m_hwKeyNote->setText(QString("This Signet also needs your YubiKey%1. Plug it in; you'll be "
					     "asked to touch it.")
				     .arg(m_hwKeySlot ? QString(" (slot %1)").arg(m_hwKeySlot) : QString()));
		m_hwKeyNote->show();
		m_useRecovery->show();
	} else {
		m_hwKeyNote->hide();
		m_useRecovery->setChecked(false);
		m_useRecovery->hide();
	}
	useRecoveryToggled(m_useRecovery->isChecked());
}

void LoginWindow::useRecoveryToggled(bool on)
{
	bool show = on && m_hwKeyRequired;
	m_recoveryLabel->setVisible(show);
	m_recoveryInput->setVisible(show);
	if (!show) {
		m_recoveryInput->clear();
	}
}

void LoginWindow::keyGenerated(int status)
{
	QByteArray key = m_keyBuilder->takeKey();
	if (!m_loggingIn || !m_preparingLogin) {
		HwKey::wipe(key);
		return;
	}
	m_preparingLogin = false;

	if (status != HwKey::STATUS_OK || key.isEmpty()) {
		HwKey::wipe(key);
		m_loggingIn = false;
		updateWidgetState();
		if (status == HwKey::STATUS_CANCELED) {
			// The user chose to stop; no need for a message.
			return;
		}
		if (status == HwKey::STATUS_OK) {
			status = HwKey::STATUS_ERROR;
		}
		SignetApplication::messageBoxError(
			status == HwKey::STATUS_BAD_SETTINGS ? QMessageBox::Critical : QMessageBox::Warning,
			"Unlock", HwKey::statusMessage((HwKey::Status)status, m_hwKeySlot), this);
		return;
	}

	updateWidgetState();
	ButtonWaitWidget *w = m_parent->beginButtonWait("unlock device", false);
	connect(w, SIGNAL(timeout()), this, SLOT(buttonWaitFinished()));
	connect(w, SIGNAL(canceled()), this, SLOT(buttonWaitFinished()));
	::signetdev_login(NULL, &m_signetdevCmdToken,
			  (u8 *)key.data(), key.length(), 0);
	// signetdev_login copies the key into its own message.
	HwKey::wipe(key);
}

void LoginWindow::buttonWaitFinished()
{
	m_loggingIn = false;
	m_preparingLogin = false;
	m_passwordInput->setText("");
	m_recoveryInput->setText("");
	updateWidgetState();
}

void LoginWindow::updateWidgetState()
{
	m_loginButton->setEnabled(!m_preparingLogin);
	m_passwordInput->setEnabled(!m_loggingIn);
	m_recoveryInput->setEnabled(!m_loggingIn);
	m_useRecovery->setEnabled(!m_loggingIn);

	if (m_preparingLogin) {
		m_preparingLabel->show();
	} else {
		m_preparingLabel->hide();
	}
}

void LoginWindow::showEvent(QShowEvent *event)
{
	Q_UNUSED(event);
	updateHardwareKeyInfo();
	this->m_passwordInput->setFocus();
}

void LoginWindow::passwordTextEdited(QString)
{
	m_incorrectPassword->hide();
}

void LoginWindow::doLogin()
{
	if (m_loggingIn) {
		return;
	}
	updateHardwareKeyInfo();
	QByteArray secret;
	if (m_hwKeyRequired && m_useRecovery->isChecked()) {
		QString text = m_recoveryInput->text();
		bool ok = HwKey::parseSecret(text, secret);
		HwKey::wipe(text);
		if (!ok) {
			SignetApplication::messageBoxError(QMessageBox::Warning, "Unlock",
				HwKey::statusMessage(HwKey::STATUS_BAD_SECRET), this);
			return;
		}
	}
	m_loggingIn = true;
	m_preparingLogin = true;
	m_incorrectPassword->hide();
	updateWidgetState();
	SignetApplication *app = SignetApplication::get();
	m_keyBuilder->start(m_passwordInput->text(), app->getHashfn(), app->getSalt(),
			    app->getKeyLength(), secret);
	HwKey::wipe(secret);
}

void LoginWindow::signetdevCmdResp(signetdevCmdRespInfo info)
{
	if (info.token != m_signetdevCmdToken) {
		return;
	}
	m_signetdevCmdToken = -1;

	int resp_code = info.resp_code;

	m_parent->endButtonWait();

	m_loggingIn = false;
	m_preparingLogin = false;
	updateWidgetState();

	switch (resp_code) {
	case OKAY:
		m_passwordInput->setText("");
		m_recoveryInput->setText("");
		m_useRecovery->setChecked(false);
		emit enterDeviceState(SignetApplication::STATE_LOGGED_IN_LOADING_ACCOUNTS);
		break;
	case BAD_PASSWORD:
		// The Signet can't tell which part was wrong.
		m_incorrectPassword->setText(m_hwKeyRequired ?
			"Incorrect password, or not the right YubiKey or recovery secret" :
			"Incorrect password");
		m_incorrectPassword->show();
		m_passwordInput->setText("");
		m_recoveryInput->setText("");
		m_passwordInput->setFocus();
		break;
	case BUTTON_PRESS_CANCELED:
	case BUTTON_PRESS_TIMEOUT:
		m_passwordInput->setText("");
		m_recoveryInput->setText("");
		break;
	case SIGNET_ERROR_DISCONNECT:
	case SIGNET_ERROR_QUIT:
		break;
	default:
		abort();
		return;
	}
}
