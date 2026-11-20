#include "yubikeyhid.h"
#include "loginkey.h"

#include <QThread>
#include <QElapsedTimer>
#include <cstring>

namespace HwKey {

namespace {

const int REPORT_DATA = 7;            // data bytes per report
const int FRAME_SIZE = 70;            // 64 data + cmd + 2 crc + 3 filler
const unsigned char SLOT_WRITE_FLAG = 0x80;   // key still busy taking our data
const unsigned char RESP_PENDING_FLAG = 0x40; // this report carries answer data
const unsigned char RESP_TIMEOUT_WAIT_FLAG = 0x20; // key is waiting for a touch
const unsigned char SEQUENCE_MASK = 0x1f;
const unsigned char RESET_REPORT = 0xff; // "stop what you're doing"

void resetState(OtpHidDevice &dev)
{
	unsigned char r[8] = {0};
	r[7] = RESET_REPORT;
	dev.setFeature(r);
}

bool awaitReady(OtpHidDevice &dev)
{
	for (int i = 0; i < 40; i++) {
		unsigned char r[8];
		if (!dev.getFeature(r)) {
			return false;
		}
		if ((r[7] & SLOT_WRITE_FLAG) == 0) {
			return true;
		}
		dev.pause(25);
	}
	return false;
}

bool canceled(const QAtomicInt *cancel)
{
	return cancel && cancel->loadAcquire() != 0;
}

} // namespace

void OtpHidDevice::pause(int ms)
{
	QThread::msleep((unsigned long)ms);
}

quint16 otpCrc(const unsigned char *data, int len)
{
	quint16 crc = 0xffff;
	for (int i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			bool bit = crc & 1;
			crc >>= 1;
			if (bit) {
				crc ^= 0x8408;
			}
		}
	}
	return crc;
}

Status otpHidCommand(OtpHidDevice &dev, unsigned char cmd, const QByteArray &payload,
		     int expectLen, QByteArray &answer, const QAtomicInt *cancel,
		     bool *touchSeen)
{
	wipe(answer);
	if (touchSeen) {
		*touchSeen = false;
	}
	if (payload.size() > 64) {
		return STATUS_ERROR;
	}

	// Build the frame.
	unsigned char frame[FRAME_SIZE];
	memset(frame, 0, sizeof(frame));
	memcpy(frame, payload.constData(), (size_t)payload.size());
	frame[64] = cmd;
	quint16 crc = otpCrc(frame, 64);
	frame[65] = (unsigned char)(crc & 0xff);
	frame[66] = (unsigned char)(crc >> 8);

	// Send it 7 bytes at a time. Empty pieces in the middle can be skipped.
	for (int seq = 0; seq < FRAME_SIZE / REPORT_DATA; seq++) {
		const unsigned char *chunk = frame + seq * REPORT_DATA;
		bool empty = true;
		for (int i = 0; i < REPORT_DATA; i++) {
			if (chunk[i]) {
				empty = false;
			}
		}
		if (empty && seq > 0 && seq < FRAME_SIZE / REPORT_DATA - 1) {
			continue;
		}
		if (!awaitReady(dev)) {
			memset(frame, 0, sizeof(frame));
			return STATUS_ERROR;
		}
		unsigned char r[8];
		memcpy(r, chunk, REPORT_DATA);
		r[7] = (unsigned char)(SLOT_WRITE_FLAG | seq);
		bool ok = dev.setFeature(r);
		memset(r, 0, sizeof(r));
		if (!ok) {
			memset(frame, 0, sizeof(frame));
			return STATUS_ERROR;
		}
	}
	memset(frame, 0, sizeof(frame));

	// Read the answer.
	QByteArray data;
	int seq = 0;
	bool needsTouch = false;
	QElapsedTimer timer;
	timer.start();
	for (;;) {
		if (canceled(cancel)) {
			resetState(dev); // the key stops blinking
			wipe(data);
			return STATUS_CANCELED;
		}
		if (timer.elapsed() > 30000) {
			// The key gives up by itself after about 15 seconds; this is
			// only a safety net.
			resetState(dev);
			wipe(data);
			return needsTouch ? STATUS_TOUCH_TIMEOUT : STATUS_ERROR;
		}
		unsigned char r[8];
		if (!dev.getFeature(r)) {
			wipe(data);
			return STATUS_ERROR;
		}
		unsigned char status = r[7];
		if (status & RESP_PENDING_FLAG) {
			if ((status & SEQUENCE_MASK) == seq) {
				data.append((const char *)r, REPORT_DATA);
				seq++;
			} else if ((status & SEQUENCE_MASK) == 0) {
				// Back to the start: the whole answer has been read.
				resetState(dev);
				break;
			}
			memset(r, 0, sizeof(r));
		} else if (status == 0) {
			// The key went back to idle without an answer.
			bool hadData = !data.isEmpty();
			wipe(data);
			if (hadData) {
				return STATUS_ERROR;
			}
			return needsTouch ? STATUS_TOUCH_TIMEOUT : STATUS_SLOT_NOT_CONFIGURED;
		} else if (status & RESP_TIMEOUT_WAIT_FLAG) {
			// Blinking, waiting for a touch.
			needsTouch = true;
			if (touchSeen) {
				*touchSeen = true;
			}
			dev.pause(100);
		} else {
			dev.pause(20);
		}
	}

	if (data.size() < expectLen + 2 ||
	    otpCrc((const unsigned char *)data.constData(), expectLen + 2) != OTP_CRC_RESIDUAL) {
		wipe(data);
		return STATUS_ERROR;
	}
	answer = data.left(expectLen);
	wipe(data);
	return STATUS_OK;
}

Status HidYubiKey::challengeResponse(int slot, const QByteArray &challenge, QByteArray &response,
				     quint32 *serial, const QAtomicInt *cancel)
{
	wipe(response);
	if (serial) {
		*serial = 0;
	}
	if ((slot != 1 && slot != 2) || challenge.size() != CHALLENGE_SIZE) {
		return STATUS_ERROR;
	}

	QList<OtpHidDevice *> devices;
	Status found = openOtpHidDevices(devices);
	if (found != STATUS_OK) {
		qDeleteAll(devices);
		return found;
	}
	if (devices.size() > 1) {
		qDeleteAll(devices);
		return STATUS_MULTIPLE_KEYS;
	}
	OtpHidDevice *dev = devices.first();

	if (serial) {
		QByteArray s;
		if (otpHidCommand(*dev, OTP_CMD_DEVICE_SERIAL, QByteArray(), 4, s, cancel) == STATUS_OK) {
			const unsigned char *b = (const unsigned char *)s.constData();
			*serial = ((quint32)b[0] << 24) | ((quint32)b[1] << 16) | ((quint32)b[2] << 8) | b[3];
		}
	}

	Status st = otpHidCommand(*dev, slot == 1 ? OTP_CMD_CHAL_HMAC1 : OTP_CMD_CHAL_HMAC2,
				  challenge, RESPONSE_SIZE, response, cancel);
	if (st != STATUS_OK) {
		wipe(response);
	}
	qDeleteAll(devices);
	return st;
}

} // namespace HwKey
