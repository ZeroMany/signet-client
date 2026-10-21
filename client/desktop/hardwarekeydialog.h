#ifndef HARDWAREKEYDIALOG_H
#define HARDWAREKEYDIALOG_H

#include <QDialog>
#include <QByteArray>

#include "signetapplication.h"

class QLabel;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QRadioButton;
class QPushButton;
class QGroupBox;
class ButtonWaitDialog;
struct signetdevCmdRespInfo;

namespace HwKey {
class Rekey;
}

// Device menu -> "YubiKey...": turn the YubiKey requirement on, replace the
// key, or turn it off again. Every change needs the master password and
// the current YubiKey (or its recovery secret), and is confirmed by
// pressing the Signet's button.
class HardwareKeyDialog : public QDialog
{
	Q_OBJECT
public:
	explicit HardwareKeyDialog(QWidget *parent = nullptr);
	~HardwareKeyDialog() override;
signals:
	void abort();
private slots:
	void updateVisibility();
	void apply();
	void keysReady(int status);
	void backupKeyNeeded();
	void buttonDialogFinished(int code);
	void signetdevCmdResp(signetdevCmdRespInfo info);
private:
	enum Action { ACTION_ENABLE, ACTION_REPLACE, ACTION_DISABLE };
	Action currentAction() const;
	void setBusy(bool busy);
	void showError(int status);

	bool m_enabledNow;
	int m_slotNow;

	QLabel *m_status;
	QLineEdit *m_password;
	QGroupBox *m_currentBox;
	QCheckBox *m_useRecovery;
	QLineEdit *m_currentRecovery;
	QGroupBox *m_actionBox;
	QRadioButton *m_replace;
	QRadioButton *m_disable;
	QGroupBox *m_newBox;
	QComboBox *m_slot;
	QRadioButton *m_proofSecret;
	QLineEdit *m_proofSecretEdit;
	QRadioButton *m_proofBackup;
	QLabel *m_working;
	QPushButton *m_applyButton;

	HwKey::Rekey *m_rekey;
	ButtonWaitDialog *m_buttonDialog;
	int m_signetdevCmdToken;
	Action m_pendingAction;
	QByteArray m_newHashfn;
	QByteArray m_newSalt;
};

#endif
