#ifndef EMPLOYEE_HANDLER_H
#define EMPLOYEE_HANDLER_H

#include "common.h"

/* Handles server-side bank employee session */
void handle_employee_session(int sock, int employee_id, const char *username);

#endif /* EMPLOYEE_HANDLER_H */
