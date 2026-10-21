#ifndef HWKEY_YUBIKEY_H
#define HWKEY_YUBIKEY_H

// Talking to a YubiKey's challenge-response slot.
//
// The real driver (PcscYubiKey) uses PC/SC, the smart-card service that is
// built into Windows and macOS and provided by "pcscd" on Linux. It works
// over USB only, never over the network. Tests swap in a pretend YubiKey
// through setResponderForTesting().

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
	// the key's serial number, or 0 if the key doesn't show it. Called on
	// a worker thread, never on the screen's thread.
	virtual Status challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
					 quint32 *serial = nullptr) = 0;
};

class PcscYubiKey : public ChallengeResponder
{
public:
	Status challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
				 quint32 *serial = nullptr) override;
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
