#include "safeactions.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QMessageBox>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

#include "signetapplication.h"

namespace SafeActions {

// A fingerprint of what we last copied, so we only clear the clipboard
// when it still holds our text. The text itself is not kept.
static QByteArray s_copiedHash;

static QByteArray hashOf(const QString &text)
{
	return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

void copySecret(const QString &text)
{
	QMimeData *mime = new QMimeData();
	mime->setText(text);
#if defined(Q_OS_WIN)
	// Keep it out of Windows clipboard history (Win+V), cloud clipboard
	// sync and clipboard monitoring programs.
	mime->setData("application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\"",
		      QByteArray(4, '\0'));
	mime->setData("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\"",
		      QByteArray(4, '\0'));
	mime->setData("application/x-qt-windows-mime;value=\"CanUploadToCloudClipboard\"",
		      QByteArray(4, '\0'));
#elif defined(Q_OS_MACOS)
	mime->setData("application/x-nspasteboard-concealed-type", QByteArray("secret"));
#else
	// KDE Klipper and other Linux clipboard managers skip this.
	mime->setData("x-kde-passwordManagerHint", QByteArray("secret"));
#endif
	QApplication::clipboard()->setMimeData(mime);

	QByteArray hash = hashOf(text);
	s_copiedHash = hash;
	QTimer::singleShot(clipboardClearSeconds * 1000, [hash]() {
		if (s_copiedHash == hash) {
			clearClipboard();
		}
	});
}

void clearClipboard()
{
	if (s_copiedHash.isEmpty()) {
		return;
	}
	QClipboard *clipboard = QApplication::clipboard();
	if (hashOf(clipboard->text()) == s_copiedHash) {
		clipboard->clear();
	}
	s_copiedHash.clear();
}

bool openWebsite(const QString &address, QWidget *parent)
{
	QString text = address.trimmed();
	if (!text.contains("://")) {
		text.prepend("https://");
	}
	QUrl url(text, QUrl::StrictMode);
	QString scheme = url.scheme().toLower();
	if (!url.isValid() || url.host().isEmpty() ||
	    (scheme != "http" && scheme != "https")) {
		SignetApplication::messageBoxError(QMessageBox::Warning, "Open website",
			"Only web addresses starting with http:// or https:// can be opened.", parent);
		return false;
	}
	return QDesktopServices::openUrl(url);
}

}
