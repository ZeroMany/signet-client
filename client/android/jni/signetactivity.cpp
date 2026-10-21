// Functions the Java side (SignetActivity.java) calls when a Signet is
// plugged in or removed. They pass the opened USB device to signetdev.
#include <jni.h>
#include <fcntl.h>
#include <stdlib.h>

#include "../signetdev_android.h"

extern "C" {

JNIEXPORT jboolean JNICALL Java_com_nthdimtech_SignetActivity_nativeReady(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	return signetdev_android_api_ready() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_nthdimtech_SignetActivity_notifyDeviceAttached(JNIEnv *env, jclass cls, jint fd, jint inEp, jint outEp, jboolean hasKeyboard, jboolean isHC)
{
	(void)env;
	(void)cls;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	struct signetdev_android_attach *m = (struct signetdev_android_attach *)malloc(sizeof(struct signetdev_android_attach));
	m->fd = fd;
	m->rx_endpoint = inEp;
	m->tx_endpoint = outEp;
	m->has_keyboard = hasKeyboard ? 1 : 0;
	m->is_hc = isHC ? 1 : 0;
	signetdev_android_attach(m);
}

JNIEXPORT void JNICALL Java_com_nthdimtech_SignetActivity_notifyDeviceDetached(JNIEnv *env, jclass cls, jint fd)
{
	(void)env;
	(void)cls;
	signetdev_android_detach(fd);
}

}
