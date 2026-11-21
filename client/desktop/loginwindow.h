#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
#include "signetapplication.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;
class MainWindow;

namespace HwKey {
class LoginKeyBuilder;
}

struct signetdevCmdRespInfo;

class LoginWindow : public QWidget
{
	Q_OBJECT
	MainWindow *m_parent;
	QLineEdit *m_passwordInput;
	QLabel *m_incorrectPassword;
	QPushButton *m_loginButton;
	HwKey::LoginKeyBuilder *m_keyBuilder;
	QLabel *m_preparingLabel;
	QLabel *m_hwKeyNote;
	QCheckBox *m_useRecovery;
	QLabel *m_recoveryLabel;
	QLineEdit *m_recoveryInput;
	bool m_loggingIn;
	bool m_preparingLogin;
	bool m_hwKeyRequired;
	int m_hwKeySlot;
	void showEvent(QShowEvent *event);
	void updateWidgetState();
	void updateHardwareKeyInfo();
	int m_signetdevCmdToken;
public:
	explicit LoginWindow(QWidget *parent = 0);
	~LoginWindow();
signals:
	void abort();
	void login(QString password);
	void enterDeviceState(int state);
public slots:
	void signetdevCmdResp(signetdevCmdRespInfo info);
	void keyGenerated(int status);
	void doLogin(void);
	void passwordTextEdited(QString);
private slots:
	void buttonWaitFinished();
	void useRecoveryToggled(bool on);
};

#endif // LOGINWINDOW_H
