#ifndef HWKEY_YUBIKEYHID_H
#define HWKEY_YUBIKEYHID_H

// The YubiKey's keyboard ("OTP HID") interface.
//
// Besides typing, this interface takes commands through 8-byte "feature
// reports": 7 bytes of data plus a status byte. A command is a 70-byte
// frame (64 bytes of data, the slot command, a checksum) sent 7 bytes at a
// time. The answer comes back the same way. While the key waits for a
// touch it says so in the status byte, and it stops waiting by itself
// after about 15 seconds.
//
// The protocol part is shared by all systems and tested with a pretend
// device. Only openOtpHidDevices() differs per system.

#include <QByteArray>
#include <QList>
#include <QAtomicInt>

#include "yubikey.h"

namespace HwKey {

class OtpHidDevice
{
public:
	virtual ~OtpHidDevice() {}
	// One 8-byte feature report, without a report-ID byte.
	virtual bool getFeature(unsigned char report[8]) = 0;
	virtual bool setFeature(const unsigned char report[8]) = 0;
	// Waits a little between polls. Tests replace it to run fast.
	virtual void pause(int ms);
};

// Finds every plugged-in YubiKey keyboard interface and opens it. The
// caller deletes the devices. Returns STATUS_OK (devices found),
// STATUS_NO_KEY, or STATUS_ACCESS_DENIED if a key was found but couldn't
// be opened.
Status openOtpHidDevices(QList<OtpHidDevice *> &devices);

// The YubiKey's checksum (CRC-16, ISO 13239). A valid answer, checksum
// included, always gives OTP_CRC_RESIDUAL.
quint16 otpCrc(const unsigned char *data, int len);
enum { OTP_CRC_RESIDUAL = 0xF0B8 };

// Sends one command and reads back `expectLen` bytes of answer (not
// counting its 2-byte checksum, which is checked). `touchSeen` is set if
// the key asked for a touch on the way.
Status otpHidCommand(OtpHidDevice &dev, unsigned char cmd, const QByteArray &payload,
		     int expectLen, QByteArray &answer, const QAtomicInt *cancel,
		     bool *touchSeen = nullptr);

// Slot commands.
enum {
	OTP_CMD_DEVICE_SERIAL = 0x10,
	OTP_CMD_CHAL_HMAC1 = 0x30,
	OTP_CMD_CHAL_HMAC2 = 0x38
};

} // namespace HwKey

#endif
