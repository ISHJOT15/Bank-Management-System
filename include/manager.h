#ifndef MANAGER_HANDLER_H
#define MANAGER_HANDLER_H

#include "common.h"

/* Handles server-side manager session */
void handle_manager_session(int sock, int manager_id, const char *username);

#endif /* MANAGER_HANDLER_H */
