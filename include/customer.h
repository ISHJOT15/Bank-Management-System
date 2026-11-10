#ifndef CUSTOMER_HANDLER_H
#define CUSTOMER_HANDLER_H

#include "common.h"

/* Handles server-side customer session */
void handle_customer_session(int sock, int customer_id, const char *username);

#endif /* CUSTOMER_HANDLER_H */
