// Opening the YubiKey's keyboard (OTP HID) interface on each system.
// Yubico's USB vendor ID is 0x1050. The OTP interface is the one that
// presents itself as a keyboard (usage page 1, usage 6).

#include "yubikeyhid.h"

#include <QVector>
#include <cstring>

namespace {
const unsigned short YUBICO_VID = 0x1050;
}

#if defined(_WIN32)
// ---------------------------------------------------------------- Windows
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

namespace HwKey {
namespace {

class WinOtpDevice : public OtpHidDevice
{
public:
	explicit WinOtpDevice(HANDLE h, int reportLen) : m_h(h), m_len(reportLen) {}
	~WinOtpDevice() override { CloseHandle(m_h); }
	bool getFeature(unsigned char report[8]) override
	{
		unsigned char buf[65] = {0};
		int len = m_len < 9 ? 9 : (m_len > 65 ? 65 : m_len);
		if (!HidD_GetFeature(m_h, buf, (ULONG)len)) {
			return false;
		}
		memcpy(report, buf + 1, 8);
		memset(buf, 0, sizeof(buf));
		return true;
	}
	bool setFeature(const unsigned char report[8]) override
	{
		unsigned char buf[65] = {0};
		int len = m_len < 9 ? 9 : (m_len > 65 ? 65 : m_len);
		memcpy(buf + 1, report, 8);
		BOOL ok = HidD_SetFeature(m_h, buf, (ULONG)len);
		memset(buf, 0, sizeof(buf));
		return ok != FALSE;
	}
private:
	HANDLE m_h;
	int m_len;
};

} // namespace

Status openOtpHidDevices(QList<OtpHidDevice *> &devices)
{
	GUID guid;
	HidD_GetHidGuid(&guid);
	HDEVINFO info = SetupDiGetClassDevsA(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (info == INVALID_HANDLE_VALUE) {
		return STATUS_NO_KEY;
	}
	bool denied = false;
	for (DWORD index = 0;; index++) {
		SP_DEVICE_INTERFACE_DATA iface;
		iface.cbSize = sizeof(iface);
		if (!SetupDiEnumDeviceInterfaces(info, NULL, &guid, index, &iface)) {
			break;
		}
		DWORD size = 0;
		SetupDiGetDeviceInterfaceDetailA(info, &iface, NULL, 0, &size, NULL);
		if (size == 0) {
			continue;
		}
		QByteArray buf((int)size, 0);
		SP_DEVICE_INTERFACE_DETAIL_DATA_A *details = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)buf.data();
		details->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
		if (!SetupDiGetDeviceInterfaceDetailA(info, &iface, details, size, NULL, NULL)) {
			continue;
		}
		// Windows keeps keyboards to itself, but opening with no read or
		// write access still allows feature reports.
		HANDLE h = CreateFileA(details->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
				       NULL, OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			continue;
		}
		HIDD_ATTRIBUTES attrib;
		attrib.Size = sizeof(attrib);
		if (!HidD_GetAttributes(h, &attrib) || attrib.VendorID != YUBICO_VID) {
			CloseHandle(h);
			continue;
		}
		PHIDP_PREPARSED_DATA pp = NULL;
		HIDP_CAPS caps;
		bool keyboard = false;
		if (HidD_GetPreparsedData(h, &pp)) {
			if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
				keyboard = caps.UsagePage == 1 && caps.Usage == 6;
			}
			HidD_FreePreparsedData(pp);
		}
		if (!keyboard) {
			CloseHandle(h);
			continue;
		}
		WinOtpDevice *dev = new WinOtpDevice(h, caps.FeatureReportByteLength);
		unsigned char probe[8];
		if (!dev->getFeature(probe)) {
			denied = true;
			delete dev;
			continue;
		}
		devices.append(dev);
	}
	SetupDiDestroyDeviceInfoList(info);
	if (!devices.isEmpty()) {
		return STATUS_OK;
	}
	return denied ? STATUS_ACCESS_DENIED : STATUS_NO_KEY;
}

} // namespace HwKey

#elif defined(__APPLE__)
// ---------------------------------------------------------------- macOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>

namespace HwKey {
namespace {

class MacOtpDevice : public OtpHidDevice
{
public:
	MacOtpDevice(IOHIDManagerRef mgr, IOHIDDeviceRef dev) : m_mgr(mgr), m_dev(dev)
	{
		CFRetain(m_mgr);
		CFRetain(m_dev);
	}
	~MacOtpDevice() override
	{
		IOHIDDeviceClose(m_dev, kIOHIDOptionsTypeNone);
		CFRelease(m_dev);
		CFRelease(m_mgr);
	}
	bool getFeature(unsigned char report[8]) override
	{
		CFIndex len = 8;
		return IOHIDDeviceGetReport(m_dev, kIOHIDReportTypeFeature, 0, report, &len) == kIOReturnSuccess &&
		       len >= 8;
	}
	bool setFeature(const unsigned char report[8]) override
	{
		return IOHIDDeviceSetReport(m_dev, kIOHIDReportTypeFeature, 0, report, 8) == kIOReturnSuccess;
	}
private:
	IOHIDManagerRef m_mgr;
	IOHIDDeviceRef m_dev;
};

void setInt(CFMutableDictionaryRef d, CFStringRef key, int value)
{
	CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
	CFDictionarySetValue(d, key, n);
	CFRelease(n);
}

} // namespace

Status openOtpHidDevices(QList<OtpHidDevice *> &devices)
{
	IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	if (!mgr) {
		return STATUS_NO_KEY;
	}
	CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	setInt(match, CFSTR(kIOHIDVendorIDKey), YUBICO_VID);
	setInt(match, CFSTR(kIOHIDPrimaryUsagePageKey), 1);
	setInt(match, CFSTR(kIOHIDPrimaryUsageKey), 6);
	IOHIDManagerSetDeviceMatching(mgr, match);
	CFRelease(match);

	bool denied = false;
	CFSetRef set = IOHIDManagerCopyDevices(mgr);
	if (set) {
		CFIndex n = CFSetGetCount(set);
		QVector<const void *> values((int)n);
		CFSetGetValues(set, values.data());
		for (CFIndex i = 0; i < n; i++) {
			IOHIDDeviceRef dev = (IOHIDDeviceRef)values[(int)i];
			IOReturn rv = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
			if (rv != kIOReturnSuccess) {
				// Usually missing "Input Monitoring" permission.
				denied = true;
				continue;
			}
			devices.append(new MacOtpDevice(mgr, dev));
		}
		CFRelease(set);
	}
	CFRelease(mgr);
	if (!devices.isEmpty()) {
		return STATUS_OK;
	}
	return denied ? STATUS_ACCESS_DENIED : STATUS_NO_KEY;
}

} // namespace HwKey

#elif defined(__linux__)
// ---------------------------------------------------------------- Linux
#include <QDir>
#include <QFile>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

namespace HwKey {
namespace {

class LinuxOtpDevice : public OtpHidDevice
{
public:
	explicit LinuxOtpDevice(int fd) : m_fd(fd) {}
	~LinuxOtpDevice() override { close(m_fd); }
	bool getFeature(unsigned char report[8]) override
	{
		unsigned char buf[9] = {0};
		if (ioctl(m_fd, HIDIOCGFEATURE(9), buf) < 0) {
			return false;
		}
		memcpy(report, buf + 1, 8);
		memset(buf, 0, sizeof(buf));
		return true;
	}
	bool setFeature(const unsigned char report[8]) override
	{
		unsigned char buf[9] = {0};
		memcpy(buf + 1, report, 8);
		int rv = ioctl(m_fd, HIDIOCSFEATURE(9), buf);
		memset(buf, 0, sizeof(buf));
		return rv >= 0;
	}
private:
	int m_fd;
};

} // namespace

Status openOtpHidDevices(QList<OtpHidDevice *> &devices)
{
	QDir dir("/sys/class/hidraw");
	bool denied = false;
	for (const QString &name : dir.entryList(QStringList() << "hidraw*", QDir::Dirs | QDir::System)) {
		QFile uevent(dir.filePath(name + "/device/uevent"));
		if (!uevent.open(QIODevice::ReadOnly)) {
			continue;
		}
		QByteArray ev = uevent.readAll().toUpper();
		// HID_ID=0003:00001050:00000407 (bus, vendor, product)
		if (!ev.contains(":00001050:")) {
			continue;
		}
		QFile desc(dir.filePath(name + "/device/report_descriptor"));
		if (!desc.open(QIODevice::ReadOnly)) {
			continue;
		}
		// Keyboard: Usage Page (Generic Desktop), Usage (Keyboard).
		if (!desc.read(4).startsWith(QByteArray("\x05\x01\x09\x06", 4))) {
			continue;
		}
		QByteArray path = QByteArray("/dev/") + name.toLatin1();
		int fd = open(path.constData(), O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			if (errno == EACCES || errno == EPERM) {
				denied = true;
			}
			continue;
		}
		devices.append(new LinuxOtpDevice(fd));
	}
	if (!devices.isEmpty()) {
		return STATUS_OK;
	}
	return denied ? STATUS_ACCESS_DENIED : STATUS_NO_KEY;
}

} // namespace HwKey

#else
namespace HwKey {
Status openOtpHidDevices(QList<OtpHidDevice *> &devices)
{
	Q_UNUSED(devices);
	return STATUS_NO_KEY;
}
}
#endif
