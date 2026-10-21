#ifndef KEYGENERATORTHREAD_H
#define KEYGENERATORTHREAD_H

#include <QObject>
#include <QThread>

class KeyGeneratorThread : public QThread
{
	Q_OBJECT
	void run();
	QString m_password;
	QByteArray m_key;
	QByteArray m_hashfn;
	QByteArray m_salt;
	int m_keyLength;
public:
	KeyGeneratorThread();
	void setParams(const QString &password, const QByteArray &hashfn, const QByteArray &salt, int keyLength);
	const QByteArray &getKey()
	{
		return m_key;
	}
	const QByteArray &getHashfn()
	{
		return m_hashfn;
	}
	const QByteArray &getSalt()
	{
		return m_salt;
	}
	// Overwrites the password and key kept by this object.
	void clearSecrets()
	{
		m_password.fill(QChar(0));
		m_password.clear();
		m_key.fill(0);
		m_key.clear();
	}
};

#endif // KEYGENERATORTHREAD_H
