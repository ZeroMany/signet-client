#include "safeactions.h"
#include "signetapplication.h"

#include "crypto/Crypto.h"

#include <QThread>
#include <QCommandLineParser>
#include <QPushButton>

#if defined(Q_OS_LINUX)
#include <sys/prctl.h>
#include <sys/resource.h>
#elif defined(Q_OS_MACOS)
#include <sys/resource.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

// If the app crashes, don't let the system save or send a copy of its
// memory, which can hold passwords from the device.
static void disableCrashDumps()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
	struct rlimit limit;
	limit.rlim_cur = 0;
	limit.rlim_max = 0;
	setrlimit(RLIMIT_CORE, &limit);
#endif
#if defined(Q_OS_LINUX)
	// Also stops other programs of the same user from reading our memory
	// with a debugger.
	prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
#if defined(Q_OS_WIN)
	SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	// Keep the heap (where passwords live) out of Windows Error Reporting
	// dumps. Looked up at run time so the build doesn't depend on werapi.h.
	typedef HRESULT (WINAPI *WerSetFlagsFn)(DWORD);
	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	WerSetFlagsFn werSetFlags = kernel ? (WerSetFlagsFn)GetProcAddress(kernel, "WerSetFlags") : nullptr;
	if (werSetFlags) {
		werSetFlags(0x00000001 /* WER_FAULT_REPORTING_FLAG_NOHEAP */);
	}
#endif
}

int main(int argc, char **argv)
{
	disableCrashDumps();
#if QT_VERSION >= 0x00050600
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
#endif
	SignetApplication *app = new SignetApplication(argc, argv);

	Crypto::init();

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
	SafeActions::clearClipboard();

	signetdev_deinitialize_api();

	delete app;

	return 0;
}
