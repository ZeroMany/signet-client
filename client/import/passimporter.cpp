#include "passimporter.h"

#ifdef Q_OS_UNIX

#include "signetapplication.h"
#include "account.h"
#include "passimportunlockdialog.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QWidget>
#include <QStringList>
#include <QTemporaryFile>

// Runs gpg with the given arguments. Each argument is passed to gpg as-is
// (no shell, no quoting), so file paths with spaces work. If a passphrase is
// given it is written to gpg's standard input rather than put on the command
// line, where other programs on the computer could read it.
// Returns gpg's exit code, or -1 if gpg could not be run.
static int runGpg(QStringList args, const QString &passphrase, QByteArray *output = nullptr)
{
	QStringList fullArgs;
	fullArgs << "--batch";
	if (passphrase.size()) {
		fullArgs << "--pinentry-mode" << "loopback" << "--passphrase-fd" << "0";
	}
	fullArgs << args;

	QProcess p;
	p.start("gpg", fullArgs);
	if (!p.waitForStarted(-1)) {
		return -1;
	}
	if (passphrase.size()) {
		QByteArray secret = passphrase.toUtf8();
		secret.append('\n');
		p.write(secret);
		secret.fill(0);
	}
	p.closeWriteChannel();
	p.waitForFinished(-1);
	if (output) {
		*output = p.readAllStandardOutput();
	}
	if (p.exitStatus() != QProcess::NormalExit) {
		return -1;
	}
	return p.exitCode();
}

PassImporter::PassImporter(QWidget *parent) :
	DatabaseImporter(parent),
	m_parent(parent)
{

}

void PassImporter::start()
{
	m_gpgId = getGPGId();

	if (!m_gpgId.size()) {
		QMessageBox *box = SignetApplication::messageBoxError(QMessageBox::Critical,
		                   databaseTypeName() + " Import",
		                   "Couldn't find valid password store",
		                   m_parent);
		connect(box, SIGNAL(destroyed(QObject *)), this, SLOT(doneFail()));
		return;
	}

	PassImportUnlockDialog *unlockDialog = new PassImportUnlockDialog(this, m_parent);
	unlockDialog->setWindowTitle(databaseTypeName()+ " Import");
	unlockDialog->setWindowModality(Qt::WindowModal);
	if (unlockDialog->exec()) {
		unlockDialog->deleteLater();
		done(false);
		return;
	}
	m_passphrase = unlockDialog->passphrase();
	m_db = new database();
	QDir root(passwordStorePath());
	m_accountType = new databaseType();
	m_db->insert("Accounts", m_accountType);
	traverse(QString(), root);
	done(true);
}

void PassImporter::doneFail()
{
	done(false);
}

void PassImporter::traverse(QString path, QDir &dir)
{
	QFileInfoList fileInfoList;
	QStringList nameFilters;
	nameFilters.append(QString("*.gpg"));
	fileInfoList = dir.entryInfoList(nameFilters, QDir::Files);
	for (auto info : fileInfoList) {
		QString accountName = info.completeBaseName();
		QStringList args;
		args << "-r" << m_gpgId << "-d" << info.absoluteFilePath();
		QByteArray decrypted;
		int rc = runGpg(args, m_passphrase, &decrypted);
		if (rc != 0) {
			QMessageBox *box = SignetApplication::messageBoxError(QMessageBox::Critical,
			                   databaseTypeName() + " Import",
			                   "Failed to decrypt " + accountName,
			                   m_parent);
			box->exec();
			box->deleteLater();
		} else {
			QString accountPassword = QString::fromUtf8(decrypted);
			decrypted.fill(0);

			// Remove the trailing newline, but only if there is one.
			if (accountPassword.endsWith('\n')) {
				accountPassword.chop(1);
			}
			account *a = new account(-1);
			a->acctName = accountName;
			a->userName = accountName;
			a->password = accountPassword;
			a->setPath(path);
			m_accountType->append(a);
		}
	}
	fileInfoList = dir.entryInfoList(QStringList(), QDir::Dirs);
	for (auto info : fileInfoList) {
		if (info.fileName() == "." || info.fileName() == "..")
			continue;
		QString subPath;
		if (path.size()) {
			subPath = path + "/" + info.fileName();
		} else {
			subPath = info.fileName();
		}
		QDir subDir(info.absoluteFilePath());
		traverse(subPath, subDir);
	}
}

QString PassImporter::passwordStorePath()
{
	QString home = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).at(0);
	return home + QString("/.password-store");
}

QString PassImporter::gpgIdPath()
{
	return passwordStorePath() + QString("/.gpg-id");
}

bool PassImporter::passphraseCheck(QString passphrase)
{
	//TODO: is there a more simpler way to test the GPG
	// with no temporary artifacts?
	QTemporaryFile t;
	t.setAutoRemove(false);
	if (!t.open()) {
		return false;
	}
	QString tmpName = t.fileName();
	t.close();
	t.remove();

	QStringList encryptArgs;
	encryptArgs << "-r" << m_gpgId << "-o" << tmpName << "-e" << gpgIdPath();
	if (runGpg(encryptArgs, QString())) {
		return false;
	}
	QStringList decryptArgs;
	decryptArgs << "-n" << "-r" << m_gpgId << "-d" << tmpName;
	bool pass = runGpg(decryptArgs, passphrase) == 0;
	QFile::remove(tmpName);
	return pass;
}

QString PassImporter::getGPGId()
{
	QString gpgIdFilePath = passwordStorePath() + QString("/.gpg-id");
	if (!QFile::exists(gpgIdFilePath))
		return QString();
	QFile gpgIdFile(gpgIdFilePath);
	if (!gpgIdFile.open(QFile::ReadOnly)) {
		return QString();
	}
	QByteArray gpgIdArray = gpgIdFile.readLine();
	QString gpgId = QString::fromLatin1(gpgIdArray);
	if (gpgId.length() != (8+1)) {
		return QString();
	}
	gpgId.truncate(8);
	bool ok;
	gpgId.toUInt(&ok, 16);
	if (!ok) {
		return QString();
	}
	QStringList args;
	args << "-n" << "-r" << gpgId << "-e" << gpgIdPath();
	int rc = runGpg(args, QString());
	if (rc != 0) {
		return QString();
	}
	return gpgId;
}

#endif
