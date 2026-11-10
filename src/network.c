#include "network.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>

/* Reliably send all bytes of a length-prefixed message */
ssize_t net_send_msg(int sock, const char *msg) {
    if (!msg || sock < 0) return -1;
    size_t len = strlen(msg);

    char hdr[16];
    int hlen = snprintf(hdr, sizeof(hdr), "%08zu\n", len);

    /* 1. Send Header */
    size_t sent = 0;
    while (sent < (size_t)hlen) {
        ssize_t n = send(sock, hdr + sent, (size_t)hlen - sent, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return -1;
        }
        sent += (size_t)n;
    }

    /* 2. Send Payload */
    sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, msg + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return -1;
        }
        sent += (size_t)n;
    }

    return (ssize_t)len;
}

/* Reliably receive a length-prefixed message */
ssize_t net_recv_msg(int sock, char *buffer, size_t max_len) {
    if (!buffer || max_len == 0 || sock < 0) return -1;
    memset(buffer, 0, max_len);

    char hdr[16];
    size_t hlen = 0;

    /* Read header until '\n' */
    while (hlen < sizeof(hdr) - 1) {
        char ch;
        ssize_t n = recv(sock, &ch, 1, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return -1;
        }
        if (ch == '\n') break;
        hdr[hlen++] = ch;
    }
    hdr[hlen] = '\0';

    size_t payload_len = (size_t)atol(hdr);
    size_t to_read = payload_len;
    if (to_read >= max_len) {
        to_read = max_len - 1;
    }

    size_t total_read = 0;
    while (total_read < to_read) {
        ssize_t n = recv(sock, buffer + total_read, to_read - total_read, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            break;
        }
        total_read += (size_t)n;
    }
    buffer[total_read] = '\0';

    /* Discard excess bytes if buffer was smaller than payload */
    if (payload_len > to_read) {
        size_t excess = payload_len - to_read;
        char discard[128];
        while (excess > 0) {
            size_t take = (excess > sizeof(discard)) ? sizeof(discard) : excess;
            ssize_t n = recv(sock, discard, take, 0);
            if (n <= 0) break;
            excess -= (size_t)n;
        }
    }

    return (ssize_t)total_read;
}
