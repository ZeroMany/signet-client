#ifndef TOUCHPROMPT_H
#define TOUCHPROMPT_H

#include <QMessageBox>
#include <QPointer>
#include <functional>
#include "hwkey/loginkeybuilder.h"

// "Touch your YubiKey now" with a Cancel button. Shown while a
// LoginKeyBuilder waits for the YubiKey. Pressing Cancel cancels the
// builder. The prompt closes and deletes itself when the builder finishes.
class TouchPrompt : public QMessageBox
{
	Q_OBJECT
public:
	TouchPrompt(HwKey::LoginKeyBuilder *builder, int slot, const QString &title,
		    const QString &extra, QWidget *parent);
	// Shows a prompt each time `builder` starts waiting for a touch.
	static void attach(HwKey::LoginKeyBuilder *builder, QWidget *parent, const QString &title,
			   const QString &extra = QString());
	// Same, but asks `extra` for the text each time (it can change per step).
	static void attach(HwKey::LoginKeyBuilder *builder, QWidget *parent, const QString &title,
			   std::function<QString()> extra);
private slots:
	void userClosed(int result);
	void builderFinished(int status);
private:
	QPointer<HwKey::LoginKeyBuilder> m_builder;
	bool m_closing;
};

#endif
