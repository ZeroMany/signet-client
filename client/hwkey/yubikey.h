#ifndef HWKEY_YUBIKEY_H
#define HWKEY_YUBIKEY_H

// Talking to a YubiKey's challenge-response slot.
//
// A YubiKey can be reached two ways over USB:
// - its keyboard ("OTP HID") interface (HidYubiKey, yubikeyhid.cpp). This
//   is the one that can wait for a touch, so it is tried first.
// - its smart-card ("CCID") interface through PC/SC (PcscYubiKey). Over
//   this one a slot that requires a touch refuses at once instead of
//   waiting, so it is only a fallback.
// YubiKeyDriver picks between them. Nothing here uses the network. Tests
// swap in a pretend YubiKey through setResponderForTesting().

#include <QAtomicInt>
#include <QByteArray>
#include <QString>

namespace HwKey {

enum Status {
	STATUS_OK = 0,
	STATUS_CANCELED,             // the user pressed Cancel
	STATUS_NO_SMARTCARD_SERVICE, // PC/SC is missing or not running
	STATUS_NO_KEY,               // no YubiKey found
	STATUS_MULTIPLE_KEYS,        // more than one YubiKey plugged in
	STATUS_SLOT_NOT_CONFIGURED,  // slot isn't set up for challenge-response
	STATUS_TOUCH_TIMEOUT,        // the key wasn't touched in time
	STATUS_TOUCH_NOT_REQUIRED,   // the slot answered without a touch
	STATUS_BAD_SETTINGS,         // the device's settings are damaged or unknown
	STATUS_BAD_SECRET,           // a typed recovery secret isn't valid
	STATUS_PROOF_MISMATCH,       // the recovery secret or backup key gives a different answer
	STATUS_SAME_KEY,             // the "backup" key is the same YubiKey as the main one
	STATUS_ACCESS_DENIED,        // the system didn't let the app open the YubiKey
	STATUS_TOUCH_UNAVAILABLE,    // the smart-card connection can't wait for a touch
	STATUS_ERROR                 // anything else went wrong talking to the key
};

// A message for the user explaining what went wrong and what to do.
QString statusMessage(Status status, int slot = 0);

class ChallengeResponder
{
public:
	virtual ~ChallengeResponder() {}
	// Sends a 64-byte challenge to the given slot (1 or 2) and waits,
	// possibly for many seconds, until the key has been touched. On
	// success `response` holds the 20-byte answer and `serial` (if given)
	// the key's serial number, or 0 if the key doesn't show it. If
	// `cancel` becomes non-zero while waiting, the driver stops (and the
	// key stops blinking) as soon as it can. Called on a worker thread,
	// never on the screen's thread.
	virtual Status challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
					 quint32 *serial = nullptr, const QAtomicInt *cancel = nullptr) = 0;
};

class PcscYubiKey : public ChallengeResponder
{
public:
	Status challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
				 quint32 *serial = nullptr, const QAtomicInt *cancel = nullptr) override;
};

class HidYubiKey : public ChallengeResponder
{
public:
	Status challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
				 quint32 *serial = nullptr, const QAtomicInt *cancel = nullptr) override;
};

// The real driver: the keyboard interface first, then the smart-card one.
class YubiKeyDriver : public ChallengeResponder
{
public:
	Status challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
				 quint32 *serial = nullptr, const QAtomicInt *cancel = nullptr) override;
private:
	HidYubiKey m_hid;
	PcscYubiKey m_pcsc;
};

// The responder the app uses: the real YubiKey driver unless a test has
// replaced it. The test keeps ownership of what it passes in; pass
// nullptr to go back to the real driver.
ChallengeResponder *responder();
void setResponderForTesting(ChallengeResponder *r);

// An answer faster than this means the slot didn't wait for a touch. A
// person can't touch the key this quickly after it starts blinking.
int minimumTouchMs();
void setMinimumTouchMsForTesting(int ms);

} // namespace HwKey

#endif
