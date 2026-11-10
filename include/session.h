#ifndef SESSION_H
#define SESSION_H

#include "common.h"

/* Session management ensuring strictly one session per user */
void session_manager_init(void);
int  session_manager_login(int user_id, int role, int sock);
void session_manager_logout(int user_id, int role);
void session_manager_cleanup_sock(int sock);
int  session_manager_is_logged_in(int user_id, int role);

#endif /* SESSION_H */
