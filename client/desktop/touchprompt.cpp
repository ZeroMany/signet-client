#include "touchprompt.h"
#include "hwkey/loginkeybuilder.h"

#include <QPointer>

TouchPrompt::TouchPrompt(HwKey::LoginKeyBuilder *builder, int slot, const QString &title,
			 const QString &extra, QWidget *parent) :
	QMessageBox(QMessageBox::Information, title, QString(), QMessageBox::Cancel, parent),
	m_builder(builder),
	m_closing(false)
{
	setWindowModality(Qt::WindowModal);
	QString text = QString("Touch your YubiKey now (slot %1). It is blinking.").arg(slot);
	if (!extra.isEmpty()) {
		text += "\n\n" + extra;
	}
	text += "\n\nIf it isn't touched in about 15 seconds, it stops waiting.";
	setText(text);
	connect(this, SIGNAL(finished(int)), this, SLOT(userClosed(int)));
	connect(builder, SIGNAL(finished(int)), this, SLOT(builderFinished(int)));
	// If the window that started this goes away, close the prompt too.
	connect(builder, &QObject::destroyed, this, [this]() { builderFinished(HwKey::STATUS_CANCELED); });
}

void TouchPrompt::attach(HwKey::LoginKeyBuilder *builder, QWidget *parent, const QString &title,
			 const QString &extra)
{
	attach(builder, parent, title, [extra]() { return extra; });
}

void TouchPrompt::attach(HwKey::LoginKeyBuilder *builder, QWidget *parent, const QString &title,
			 std::function<QString()> extra)
{
	QPointer<QWidget> p(parent);
	QObject::connect(builder, &HwKey::LoginKeyBuilder::touchNeeded, parent,
			 [builder, p, title, extra](int slot) {
		if (!p) {
			return;
		}
		TouchPrompt *prompt = new TouchPrompt(builder, slot, title, extra(), p);
		prompt->show();
	});
}

void TouchPrompt::userClosed(int result)
{
	Q_UNUSED(result);
	if (m_closing) {
		return;
	}
	m_closing = true;
	// The user pressed Cancel or closed the window: stop waiting.
	if (m_builder) {
		disconnect(m_builder, nullptr, this, nullptr);
		m_builder->cancel();
	}
	deleteLater();
}

void TouchPrompt::builderFinished(int status)
{
	Q_UNUSED(status);
	if (m_closing) {
		return;
	}
	m_closing = true;
	if (m_builder) {
		disconnect(m_builder, nullptr, this, nullptr);
	}
	done(QMessageBox::Ok);
	deleteLater();
}
