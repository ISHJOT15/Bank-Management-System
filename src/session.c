#include "session.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

#define MAX_SESSIONS 256

typedef struct {
    int user_id;
    int role;
    int sock;
} ActiveSession;

static ActiveSession g_sessions[MAX_SESSIONS];
static int g_session_count = 0;
static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t g_session_sem;
static int g_initialized = 0;

void session_manager_init(void) {
    pthread_mutex_lock(&g_session_mutex);
    if (!g_initialized) {
        g_session_count = 0;
        sem_init(&g_session_sem, 0, MAX_SESSIONS);
        g_initialized = 1;
    }
    pthread_mutex_unlock(&g_session_mutex);
}

int session_manager_is_logged_in(int user_id, int role) {
    int found = 0;
    pthread_mutex_lock(&g_session_mutex);
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].user_id == user_id && g_sessions[i].role == role) {
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
    return found;
}

int session_manager_login(int user_id, int role, int sock) {
    session_manager_init();

    pthread_mutex_lock(&g_session_mutex);
    /* Check if already logged in */
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].user_id == user_id && g_sessions[i].role == role) {
            pthread_mutex_unlock(&g_session_mutex);
            return 0; /* Login rejected: active session already exists */
        }
    }

    if (g_session_count >= MAX_SESSIONS) {
        pthread_mutex_unlock(&g_session_mutex);
        return 0;
    }

    /* Decrement available session capacity semaphore */
    sem_wait(&g_session_sem);

    g_sessions[g_session_count].user_id = user_id;
    g_sessions[g_session_count].role = role;
    g_sessions[g_session_count].sock = sock;
    g_session_count++;

    pthread_mutex_unlock(&g_session_mutex);
    return 1; /* Login allowed */
}

void session_manager_logout(int user_id, int role) {
    pthread_mutex_lock(&g_session_mutex);
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].user_id == user_id && g_sessions[i].role == role) {
            g_sessions[i] = g_sessions[g_session_count - 1];
            g_session_count--;
            sem_post(&g_session_sem);
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
}

void session_manager_cleanup_sock(int sock) {
    if (sock <= 0) return;
    pthread_mutex_lock(&g_session_mutex);
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].sock == sock) {
            g_sessions[i] = g_sessions[g_session_count - 1];
            g_session_count--;
            sem_post(&g_session_sem);
            i--; /* Recheck current index */
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
}
