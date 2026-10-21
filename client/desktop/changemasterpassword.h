#ifndef CHANGEMASTERPASSWORD_H
#define CHANGEMASTERPASSWORD_H

#include <QDialog>
#include <QString>

#include "signetapplication.h"

class CommThread;
class QLabel;
class QLineEdit;
class ButtonWaitDialog;
class QPushButton;
struct signetdevCmdRespInfo;
class QSpinBox;
namespace HwKey {
class Rekey;
}

class ChangeMasterPassword : public QDialog
{
	Q_OBJECT
	QLabel *m_newPasswordWarningMessage;
	QLabel *m_oldPasswordWarningMessage;
	QLabel *m_generatingKeys;
	QLineEdit *m_oldPasswordEdit;
	QLineEdit *m_newPasswordEdit;
	QLineEdit *m_newPasswordRepeatEdit;
	ButtonWaitDialog *m_buttonDialog;
	QPushButton *m_changePasswordBtn;
	HwKey::Rekey *m_rekey;
	QByteArray m_newHashfn;
	QByteArray m_newSalt;
	int m_signetdevCmdToken;
	QLabel *m_securityLevelComment;
	QSpinBox *m_authSecurityLevel;
	void setInputsEnabled(bool enabled);
public:
	ChangeMasterPassword(QWidget *parent = 0);
	virtual ~ChangeMasterPassword();
signals:
	void abort();
public slots:
	void signetdevCmdResp(signetdevCmdRespInfo info);
	void keysReady(int status);
	void changePasswordFinished(int);
	void changePasswordUi();
	void newPasswordTextEdited(QString);
	void oldPasswordTextEdited(QString);
};

#endif // CHANGEMASTERPASSWORD_H
