package com.nthdimtech;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;

import org.qtproject.qt5.android.bindings.QtActivity;

/*
 * The app's main screen. Qt draws everything; this class only adds USB
 * handling. Android apps can't open USB devices directly, so we ask the
 * user for permission, open the Signet here and pass its connection to
 * the C code (client/android/jni/signetactivity.cpp).
 */
public class SignetActivity extends QtActivity {
	private static final String TAG = "SIGNET_ACTIVITY";
	private static final String ACTION_USB_PERMISSION = "com.nthdimtech.SignetClient.USB_PERMISSION";

	// Original Signet
	private static final int SIGNET_VID = 0x5E2A;
	private static final int SIGNET_PID = 0x0001;
	// Signet HC
	private static final int SIGNET_HC_VID = 0x1209;
	private static final int SIGNET_HC_PID = 0xDF11;
	// The Signet HC's command interface is interface number 1
	private static final int SIGNET_HC_HID_INTERFACE = 1;

	// PendingIntent.FLAG_MUTABLE (Android 12+). The system adds the
	// device to the permission reply, so the intent must stay mutable.
	private static final int FLAG_MUTABLE = 0x02000000;

	private UsbManager mManager;
	private PendingIntent mPermissionIntent;
	private Handler mHandler;

	private UsbDevice mDevice;
	private UsbDeviceConnection mConnection;
	private UsbInterface mHidInterface;
	private int mFd = -1;

	private static native boolean nativeReady();
	private static native void notifyDeviceAttached(int fd, int inEp, int outEp, boolean hasKeyboard, boolean isHC);
	private static native void notifyDeviceDetached(int fd);

	private static boolean isSignet(UsbDevice device) {
		return isOriginal(device) || isHC(device);
	}

	private static boolean isOriginal(UsbDevice device) {
		return device.getVendorId() == SIGNET_VID && device.getProductId() == SIGNET_PID;
	}

	private static boolean isHC(UsbDevice device) {
		return device.getVendorId() == SIGNET_HC_VID && device.getProductId() == SIGNET_HC_PID;
	}

	private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {
		@Override
		public void onReceive(Context context, Intent intent) {
			String action = intent.getAction();
			UsbDevice device = (UsbDevice) intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
			if (device == null || !isSignet(device)) {
				return;
			}
			if (ACTION_USB_PERMISSION.equals(action)) {
				if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
					Log.d(TAG, "USB permission granted");
					openDevice(device);
				} else {
					Log.d(TAG, "USB permission denied");
				}
			} else if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
				Log.d(TAG, "Signet attached");
				useDevice(device);
			} else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
				Log.d(TAG, "Signet detached");
				if (mDevice != null && mDevice.getDeviceId() == device.getDeviceId()) {
					closeDevice();
				}
			}
		}
	};

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		mHandler = new Handler();
		mManager = (UsbManager) getSystemService(Context.USB_SERVICE);
		int flags = Build.VERSION.SDK_INT >= 31 ? FLAG_MUTABLE : 0;
		Intent permissionIntent = new Intent(ACTION_USB_PERMISSION);
		permissionIntent.setPackage(getPackageName());
		mPermissionIntent = PendingIntent.getBroadcast(this, 0, permissionIntent, flags);
		IntentFilter filter = new IntentFilter();
		filter.addAction(ACTION_USB_PERMISSION);
		filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
		filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
		registerReceiver(mUsbReceiver, filter);
		checkForDevices();
	}

	@Override
	public void onNewIntent(Intent intent) {
		super.onNewIntent(intent);
		// Plugging in a Signet while the app is open lands here.
		if (intent != null && UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(intent.getAction())) {
			UsbDevice device = (UsbDevice) intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
			if (device != null && isSignet(device)) {
				useDevice(device);
			}
		}
	}

	@Override
	public void onDestroy() {
		try {
			unregisterReceiver(mUsbReceiver);
		} catch (IllegalArgumentException e) {
			// Not registered
		}
		closeDevice();
		super.onDestroy();
	}

	// Look for a Signet that was plugged in before the app started.
	// Waits until the C side is ready to receive it.
	private void checkForDevices() {
		if (!nativeReadySafe()) {
			mHandler.postDelayed(new Runnable() {
				@Override
				public void run() {
					checkForDevices();
				}
			}, 250);
			return;
		}
		for (UsbDevice device : mManager.getDeviceList().values()) {
			if (isSignet(device)) {
				Log.d(TAG, "Found " + device);
				useDevice(device);
				return;
			}
		}
	}

	private boolean nativeReadySafe() {
		try {
			return nativeReady();
		} catch (UnsatisfiedLinkError e) {
			// The C++ library hasn't loaded yet
			return false;
		}
	}

	private void useDevice(UsbDevice device) {
		if (mDevice != null) {
			return;
		}
		if (mManager.hasPermission(device)) {
			openDevice(device);
		} else {
			Log.d(TAG, "Requesting USB permission");
			mManager.requestPermission(device, mPermissionIntent);
		}
	}

	private void openDevice(final UsbDevice device) {
		if (mDevice != null) {
			return;
		}
		if (!nativeReadySafe()) {
			mHandler.postDelayed(new Runnable() {
				@Override
				public void run() {
					openDevice(device);
				}
			}, 250);
			return;
		}
		boolean hc = isHC(device);
		UsbInterface hidInterface = null;
		UsbEndpoint inEp = null;
		UsbEndpoint outEp = null;
		boolean hasKeyboard = false;

		for (int i = 0; i < device.getInterfaceCount(); i++) {
			UsbInterface intf = device.getInterface(i);
			if (intf.getInterfaceClass() != UsbConstants.USB_CLASS_HID) {
				continue;
			}
			if (intf.getInterfaceProtocol() == 1 && intf.getInterfaceSubclass() == 1) {
				hasKeyboard = true;
				continue;
			}
			if (hidInterface != null || intf.getInterfaceProtocol() != 0) {
				continue;
			}
			if (hc && intf.getId() != SIGNET_HC_HID_INTERFACE) {
				continue;
			}
			UsbEndpoint in = null;
			UsbEndpoint out = null;
			for (int j = 0; j < intf.getEndpointCount(); j++) {
				UsbEndpoint ep = intf.getEndpoint(j);
				if (ep.getDirection() == UsbConstants.USB_DIR_OUT) {
					out = ep;
				} else {
					in = ep;
				}
			}
			if (in != null && out != null) {
				hidInterface = intf;
				inEp = in;
				outEp = out;
			}
		}
		if (hidInterface == null) {
			Log.d(TAG, "No Signet command interface found");
			return;
		}
		UsbDeviceConnection connection = mManager.openDevice(device);
		if (connection == null) {
			Log.d(TAG, "Couldn't open device");
			return;
		}
		if (!connection.claimInterface(hidInterface, true)) {
			Log.d(TAG, "Couldn't claim interface");
			connection.close();
			return;
		}
		mDevice = device;
		mConnection = connection;
		mHidInterface = hidInterface;
		mFd = connection.getFileDescriptor();
		Log.d(TAG, "Opened " + (hc ? "Signet HC" : "Signet") + ", fd " + mFd);
		notifyDeviceAttached(mFd, inEp.getEndpointNumber(), outEp.getEndpointNumber(), hasKeyboard, hc);
	}

	private void closeDevice() {
		if (mDevice == null) {
			return;
		}
		if (nativeReadySafe()) {
			notifyDeviceDetached(mFd);
		}
		if (mConnection != null) {
			mConnection.releaseInterface(mHidInterface);
			mConnection.close();
		}
		mDevice = null;
		mConnection = null;
		mHidInterface = null;
		mFd = -1;
	}
}
