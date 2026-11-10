#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include <sys/types.h>

/* Safe socket communication helpers using system calls */
ssize_t net_send_msg(int sock, const char *msg);
ssize_t net_recv_msg(int sock, char *buffer, size_t max_len);

#endif /* NETWORK_H */
