#include "yubikey.h"
#include "loginkey.h"

#include <QAtomicPointer>
#include <QAtomicInt>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <winscard.h>
#define HWKEY_LIST_READERS SCardListReadersA
#define HWKEY_CONNECT SCardConnectA
typedef LPSTR hwkey_reader_list_t;
#elif defined(__APPLE__)
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#define HWKEY_LIST_READERS SCardListReaders
#define HWKEY_CONNECT SCardConnect
typedef char *hwkey_reader_list_t;
#else
#include <winscard.h>
#define HWKEY_LIST_READERS SCardListReaders
#define HWKEY_CONNECT SCardConnect
typedef char *hwkey_reader_list_t;
#endif

namespace HwKey {

namespace {

QAtomicPointer<ChallengeResponder> g_testResponder;
QAtomicInt g_minimumTouchMs(250);

// The YubiKey "OTP" application, which holds the two challenge-response
// slots. Selecting it by this ID also tells us the card is a YubiKey.
const unsigned char OTP_AID[] = {0xA0, 0x00, 0x00, 0x05, 0x27, 0x20, 0x01};
const unsigned char INS_SELECT = 0xA4;
const unsigned char INS_API_REQ = 0x01;
const unsigned char INS_GET_RESPONSE = 0xC0;
const unsigned char CMD_GET_SERIAL = 0x10;
const unsigned char CMD_HMAC_SLOT1 = 0x30;
const unsigned char CMD_HMAC_SLOT2 = 0x38;

struct Card {
	SCARDHANDLE handle = 0;
	DWORD protocol = 0;
};

const SCARD_IO_REQUEST *pciFor(DWORD protocol)
{
	return protocol == SCARD_PROTOCOL_T0 ? SCARD_PCI_T0 : SCARD_PCI_T1;
}

// Sends one command (APDU) and collects the full answer, including the
// 2-byte status word at the end. Returns false if the card didn't answer.
bool transmit(const Card &card, const QByteArray &apdu, QByteArray &reply)
{
	reply.clear();
	QByteArray cmd = apdu;
	for (int round = 0; round < 8; round++) {
		unsigned char buf[258];
		DWORD len = sizeof(buf);
		LONG rv = SCardTransmit(card.handle, pciFor(card.protocol),
					(const unsigned char *)cmd.constData(), (DWORD)cmd.size(),
					nullptr, buf, &len);
		if (rv != (LONG)SCARD_S_SUCCESS || len < 2) {
			wipe(cmd);
			return false;
		}
		unsigned char sw1 = buf[len - 2];
		unsigned char sw2 = buf[len - 1];
		reply.append((const char *)buf, (int)len - 2);
		memset(buf, 0, sizeof(buf));
		if (sw1 == 0x61) {
			// More answer waiting: ask for the rest.
			wipe(cmd);
			cmd = QByteArray(5, 0);
			cmd[1] = (char)INS_GET_RESPONSE;
			cmd[4] = (char)sw2;
			continue;
		}
		reply.append((char)sw1);
		reply.append((char)sw2);
		wipe(cmd);
		return true;
	}
	wipe(cmd);
	return false;
}

unsigned int statusWord(const QByteArray &reply)
{
	int n = reply.size();
	return ((unsigned int)(unsigned char)reply.at(n - 2) << 8) | (unsigned char)reply.at(n - 1);
}

bool selectOtp(const Card &card)
{
	QByteArray apdu;
	apdu.append((char)0x00);
	apdu.append((char)INS_SELECT);
	apdu.append((char)0x04);
	apdu.append((char)0x00);
	apdu.append((char)sizeof(OTP_AID));
	apdu.append((const char *)OTP_AID, sizeof(OTP_AID));
	QByteArray reply;
	return transmit(card, apdu, reply) && statusWord(reply) == 0x9000;
}

// Closes the connection and the PC/SC session when it goes out of scope.
struct Session {
	SCARDCONTEXT ctx = 0;
	bool haveCtx = false;
	Card card;
	bool haveCard = false;
	~Session()
	{
		if (haveCard) {
			SCardDisconnect(card.handle, SCARD_LEAVE_CARD);
		}
		if (haveCtx) {
			SCardReleaseContext(ctx);
		}
	}
};

} // namespace

ChallengeResponder *responder()
{
	static PcscYubiKey real;
	ChallengeResponder *t = g_testResponder.loadAcquire();
	return t ? t : &real;
}

void setResponderForTesting(ChallengeResponder *r)
{
	g_testResponder.storeRelease(r);
}

int minimumTouchMs()
{
	return g_minimumTouchMs.loadAcquire();
}

void setMinimumTouchMsForTesting(int ms)
{
	g_minimumTouchMs.storeRelease(ms);
}

namespace {

// The key's serial number, or 0 if it doesn't show it.
quint32 readSerial(const Card &card)
{
	QByteArray apdu(5, 0);
	apdu[1] = (char)INS_API_REQ;
	apdu[2] = (char)CMD_GET_SERIAL;
	apdu[4] = 4;
	QByteArray reply;
	if (!transmit(card, apdu, reply) || statusWord(reply) != 0x9000 || reply.size() < 6) {
		return 0;
	}
	const unsigned char *b = (const unsigned char *)reply.constData();
	return ((quint32)b[0] << 24) | ((quint32)b[1] << 16) | ((quint32)b[2] << 8) | b[3];
}

} // namespace

Status PcscYubiKey::challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
				      quint32 *serial)
{
	wipe(response);
	if (serial) {
		*serial = 0;
	}
	if ((slot != 1 && slot != 2) || challenge.size() != CHALLENGE_SIZE) {
		return STATUS_ERROR;
	}

	Session s;
	LONG rv = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &s.ctx);
	if (rv != (LONG)SCARD_S_SUCCESS) {
		return STATUS_NO_SMARTCARD_SERVICE;
	}
	s.haveCtx = true;

	DWORD listLen = 0;
	rv = HWKEY_LIST_READERS(s.ctx, nullptr, nullptr, &listLen);
	if (rv == (LONG)SCARD_E_NO_READERS_AVAILABLE || (rv == (LONG)SCARD_S_SUCCESS && listLen <= 1)) {
		return STATUS_NO_KEY;
	}
	if (rv == (LONG)SCARD_E_NO_SERVICE
#ifdef SCARD_E_SERVICE_STOPPED
	    || rv == (LONG)SCARD_E_SERVICE_STOPPED
#endif
	   ) {
		return STATUS_NO_SMARTCARD_SERVICE;
	}
	if (rv != (LONG)SCARD_S_SUCCESS) {
		return STATUS_ERROR;
	}
	QByteArray names((int)listLen, 0);
	rv = HWKEY_LIST_READERS(s.ctx, nullptr, (hwkey_reader_list_t)names.data(), &listLen);
	if (rv == (LONG)SCARD_E_NO_READERS_AVAILABLE) {
		return STATUS_NO_KEY;
	}
	if (rv != (LONG)SCARD_S_SUCCESS) {
		return STATUS_ERROR;
	}

	// The list is "name\0name\0\0". Find every reader holding a YubiKey.
	int found = 0;
	const char *p = names.constData();
	const char *end = p + names.size();
	while (p < end && *p) {
		Card c;
		rv = HWKEY_CONNECT(s.ctx, p, SCARD_SHARE_SHARED,
				   SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &c.handle, &c.protocol);
		if (rv == (LONG)SCARD_S_SUCCESS) {
			if (selectOtp(c)) {
				found++;
				if (!s.haveCard) {
					s.card = c;
					s.haveCard = true;
					c.handle = 0;
				}
			}
			if (c.handle) {
				SCardDisconnect(c.handle, SCARD_LEAVE_CARD);
			}
		}
		p += strlen(p) + 1;
	}
	if (found == 0) {
		return STATUS_NO_KEY;
	}
	if (found > 1) {
		// Don't guess which one the user meant.
		return STATUS_MULTIPLE_KEYS;
	}

	if (serial) {
		*serial = readSerial(s.card);
	}
	// Select again right before asking, in case something else used the
	// key in between.
	if (!selectOtp(s.card)) {
		return STATUS_ERROR;
	}
	QByteArray apdu;
	apdu.append((char)0x00);
	apdu.append((char)INS_API_REQ);
	apdu.append((char)(slot == 1 ? CMD_HMAC_SLOT1 : CMD_HMAC_SLOT2));
	apdu.append((char)0x00);
	apdu.append((char)CHALLENGE_SIZE);
	apdu.append(challenge);

	// This waits until the key is touched, or the key gives up (about
	// 15 seconds).
	QByteArray reply;
	bool ok = transmit(s.card, apdu, reply);
	wipe(apdu);
	if (!ok) {
		wipe(reply);
		return STATUS_ERROR;
	}
	unsigned int sw = statusWord(reply);
	Status result;
	if (sw == 0x9000 && reply.size() == RESPONSE_SIZE + 2) {
		response = reply.left(RESPONSE_SIZE);
		result = STATUS_OK;
	} else if (sw == 0x9000) {
		// Answered, but without data: the slot is empty or holds
		// something other than challenge-response.
		result = STATUS_SLOT_NOT_CONFIGURED;
	} else if (sw == 0x6985) {
		result = STATUS_TOUCH_TIMEOUT;
	} else if ((sw & 0xff00) == 0x6a00 || (sw & 0xff00) == 0x6d00) {
		result = STATUS_SLOT_NOT_CONFIGURED;
	} else {
		result = STATUS_ERROR;
	}
	wipe(reply);
	return result;
}

QString statusMessage(Status status, int slot)
{
	QString slotText = slot ? QString("slot %1").arg(slot) : QString("the chosen slot");
	switch (status) {
	case STATUS_OK:
		return QString();
	case STATUS_CANCELED:
		return "Canceled. Nothing was sent to your Signet.";
	case STATUS_NO_SMARTCARD_SERVICE:
#if defined(__linux__)
		return "The smart-card service (pcscd) isn't running, or isn't letting this app use it, so the "
		       "app can't talk to your YubiKey. Install and start it, for example: sudo apt install pcscd, "
		       "then sudo systemctl enable --now pcscd.socket";
#else
		return "The computer's smart-card service isn't available, so the app can't talk to your YubiKey.";
#endif
	case STATUS_NO_KEY:
		return "No YubiKey found. Plug it in and try again. (Its smart-card interface, CCID, must be "
		       "switched on. It is by default.)";
	case STATUS_MULTIPLE_KEYS:
		return "More than one YubiKey is plugged in. Leave only the one you want to use, and try again.";
	case STATUS_SLOT_NOT_CONFIGURED:
		return QString("Your YubiKey's %1 isn't set up for challenge-response. Set it up in YubiKey "
			       "Manager (Applications, OTP, Challenge-response) with 'Require touch' ticked.").arg(slotText);
	case STATUS_TOUCH_TIMEOUT:
		return "The YubiKey wasn't touched in time. Try again and touch the key while it blinks.";
	case STATUS_TOUCH_NOT_REQUIRED:
		return QString("Your YubiKey's %1 answered without being touched. For safety the app only "
			       "accepts a slot that requires a touch. Turn on 'Require touch' for this slot in "
			       "YubiKey Manager, keeping the same secret.").arg(slotText);
	case STATUS_BAD_SETTINGS:
		return "Your Signet reported login settings this app doesn't recognise or that look damaged, "
		       "so nothing was sent to it. If this is your own Signet, it may be faulty or tampered with.";
	case STATUS_BAD_SECRET:
		return "That recovery secret isn't valid. It should be the slot's secret in hexadecimal "
		       "(0-9 and a-f), up to 40 characters.";
	case STATUS_PROOF_MISMATCH:
		return "That doesn't give the same answer as your YubiKey, so it couldn't be used to recover "
		       "your vault. Check the secret (or that the backup key's slot holds the same secret) and try again.";
	case STATUS_SAME_KEY:
		return "That is the same YubiKey. Unplug it and plug in your backup YubiKey instead.";
	case STATUS_ERROR:
	default:
		return "Something went wrong talking to the YubiKey. Unplug it, plug it back in, and try again.";
	}
}

} // namespace HwKey
