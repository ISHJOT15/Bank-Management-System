#ifndef ADMIN_HANDLER_H
#define ADMIN_HANDLER_H

#include "common.h"

/* Handles server-side administrator session */
void handle_admin_session(int sock, int admin_id, const char *username);

#endif /* ADMIN_HANDLER_H */
