/*
 * Replacement for signet-base's signetdev_emulate.c.
 *
 * The original file let the app open a backup file and treat it like a
 * Signet device. This build removes that: passwords can only be read from
 * a real, connected device. The platform code still references this
 * function, so we keep a version that does nothing but free the message.
 * It is never reached, because emulation can no longer be turned on.
 */
#include "signetdev/host/signetdev_priv.h"

int signetdev_emulate_handle_message_priv(struct send_message_req *msg)
{
	signetdev_priv_free_message(&msg);
	return 0;
}
