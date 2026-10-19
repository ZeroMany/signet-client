#include "signetapplication.h"

#ifndef SIGNET_NO_KEEPASS
#include "crypto/Crypto.h"
#endif

#include <QThread>
#include <QCommandLineParser>
#include <QPushButton>

int main(int argc, char **argv)
{
#ifdef Q_OS_ANDROID
	// Phones have dense screens, so let Qt scale everything up.
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#elif QT_VERSION >= 0x00050600
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
#endif
	SignetApplication *app = new SignetApplication(argc, argv);

#ifndef SIGNET_NO_KEEPASS
	Crypto::init();
#endif

#ifndef Q_OS_ANDROID
	if (app->isRunning()) {
		QIcon signetIcon = QIcon(":/images/signet.png");
		QMessageBox *box = new QMessageBox(QMessageBox::Information, "Signet",
						   "A Signet client is already running."
						   "Only one instance of the client can run at the same time.",
                           nullptr,
                           nullptr,
						   Qt::WindowStaysOnTopHint);
		QPushButton *closeItButton = box->addButton("Replace running instance", QMessageBox::RejectRole);
		box->addButton("Switch to running instance", QMessageBox::AcceptRole);
		box->setWindowIcon(signetIcon);
		box->exec();
		QAbstractButton *clickedButton = box->clickedButton();
		box->deleteLater();
		box = nullptr;
		if (clickedButton == static_cast<QAbstractButton *>(closeItButton)) {
			app->sendMessage("close");
			int i = 0;
			while(app->isRunning()) {
				QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
				i++;
				if (i > 200) {
					box = new QMessageBox(QMessageBox::Information, "Signet",
                                  "Existing signet client is not closing. Close it and try again",
                                  QMessageBox::Ok,
                                  nullptr,
                                  Qt::WindowStaysOnTopHint);
					box->exec();
					box->deleteLater();
					box = NULL;
					return 0;
				}
				QThread::usleep(100000);
			}
		} else {
			app->sendMessage("open");
			return 0;
		}
		box->deleteLater();
	}
#endif

	QCommandLineParser parser;
	parser.addHelpOption();
#ifdef Q_OS_UNIX
	QCommandLineOption startInTrayOption("start-in-systray", "Start application minimized in the system tray");
	parser.addOption(startInTrayOption);
#endif
	parser.process(*app);

#ifdef Q_OS_UNIX
	bool startInTray = parser.isSet(startInTrayOption);
#else
	bool startInTray = false;
#endif

	app->setQuitOnLastWindowClosed(false);

	app->init(startInTray);
	app->exec();

	signetdev_deinitialize_api();

	delete app;

	return 0;
}
