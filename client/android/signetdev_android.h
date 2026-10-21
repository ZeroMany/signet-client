#ifndef SIGNETDEV_ANDROID_H
#define SIGNETDEV_ANDROID_H

/* Glue between the Java USB code and the signetdev Android backend. */

#ifdef __cplusplus
extern "C" {
#endif

struct signetdev_android_attach {
	int fd;
	int rx_endpoint;
	int tx_endpoint;
	int has_keyboard;
	int is_hc;
};

/* Non-zero once signetdev_initialize_api() has run, so the Java side
 * knows it can hand over a device. */
int signetdev_android_api_ready();

/* attach() frees m when done. The USB descriptor itself stays owned by
 * the Java side, which closes it. */
void signetdev_android_attach(struct signetdev_android_attach *m);
void signetdev_android_detach(int fd);

#ifdef __cplusplus
}
#endif

#endif
