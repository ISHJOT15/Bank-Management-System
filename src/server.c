#include "common.h"
#include "network.h"
#include "session.h"
#include "db.h"
#include "customer.h"
#include "employee.h"
#include "manager.h"
#include "admin.h"
#include <signal.h>

static int g_server_sock = -1;

void handle_sigint(int sig) {
    (void)sig;
    printf("\n[Server] Shutting down Banking Management System Server...\n");
    if (g_server_sock >= 0) {
        close(g_server_sock);
    }
    exit(0);
}

typedef struct {
    int sock;
    struct sockaddr_in addr;
} client_conn_t;

void *client_worker(void *arg) {
    client_conn_t *conn = (client_conn_t *)arg;
    int sock = conn->sock;
    pthread_detach(pthread_self()); /* Automatically reclaim thread resources on termination */

    char in_buf[BUFFER_SIZE];
    char out_buf[BUFFER_SIZE];

    while (1) {
        /* Wait for role selection or disconnect */
        if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) {
            break; /* Client disconnected */
        }

        int role_choice = atoi(in_buf);
        if (role_choice == 5) {
            /* Client selected to Disconnect/Exit from main role screen */
            net_send_msg(sock, "DISCONNECT_OK\n");
            break;
        }

        if (role_choice < 1 || role_choice > 4) {
            net_send_msg(sock, "ERROR: Invalid role selected.\n");
            continue;
        }

        /* Prompt for Username */
        net_send_msg(sock, "PROMPT:Enter username: ");
        if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
        char username[NAME_LEN];
        safe_copy_str(username, sizeof(username), in_buf);

        /* Prompt for Password */
        net_send_msg(sock, "PROMPT:Enter password: ");
        if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
        char password[PASS_LEN];
        safe_copy_str(password, sizeof(password), in_buf);

        int user_id = 0;
        int auth_status = 0;

        if (role_choice == ROLE_ADMIN) {
            Admin a;
            auth_status = db_auth_admin(username, password, &a);
            if (auth_status > 0) user_id = a.id;
        } else if (role_choice == ROLE_MANAGER) {
            Manager m;
            auth_status = db_auth_manager(username, password, &m);
            if (auth_status > 0) user_id = m.id;
        } else if (role_choice == ROLE_EMPLOYEE) {
            Employee e;
            auth_status = db_auth_employee(username, password, &e);
            if (auth_status > 0) user_id = e.id;
        } else if (role_choice == ROLE_CUSTOMER) {
            Customer c;
            auth_status = db_auth_customer(username, password, &c);
            if (auth_status > 0) {
                user_id = c.id;
            } else if (auth_status == -2) {
                net_send_msg(sock, "ERROR: Account is deactivated. Please contact your branch manager.\n");
                continue;
            }
        }

        if (auth_status <= 0) {
            net_send_msg(sock, "ERROR: Invalid credentials. Authentication failed.\n");
            continue;
        }

        /* Check and enforce ONE SESSION PER USER */
        if (!session_manager_login(user_id, role_choice, sock)) {
            net_send_msg(sock, "ERROR: User already logged in another active session (Single Session Policy enforced)!\n");
            continue;
        }

        /* Login Succeeded */
        snprintf(out_buf, sizeof(out_buf), "AUTH_OK:%d:%s\n", user_id, username);
        net_send_msg(sock, out_buf);
        printf("[Server] User '%s' (ID: %d, Role: %d) successfully logged in.\n",
               username, user_id, role_choice);

        /* Route to Role-Specific Session Handler */
        if (role_choice == ROLE_CUSTOMER) {
            handle_customer_session(sock, user_id, username);
        } else if (role_choice == ROLE_EMPLOYEE) {
            handle_employee_session(sock, user_id, username);
        } else if (role_choice == ROLE_MANAGER) {
            handle_manager_session(sock, user_id, username);
        } else if (role_choice == ROLE_ADMIN) {
            handle_admin_session(sock, user_id, username);
        }

        /* When user logs out or terminates session */
        session_manager_logout(user_id, role_choice);
        printf("[Server] User '%s' (ID: %d) session terminated.\n", username, user_id);
    }

    session_manager_cleanup_sock(sock);
    close(sock);
    free(conn);
    return NULL;
}

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGPIPE, SIG_IGN); /* Prevent SIGPIPE from terminating server on client disconnect */

    session_manager_init();

    /* Create TCP Socket using system call */
    g_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_sock < 0) {
        perror("Failed to create server socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(g_server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    /* Bind system call */
    if (bind(g_server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed on port");
        close(g_server_sock);
        exit(EXIT_FAILURE);
    }

    /* Listen system call */
    if (listen(g_server_sock, 50) < 0) {
        perror("Listen failed");
        close(g_server_sock);
        exit(EXIT_FAILURE);
    }

    printf("=================================================================\n");
    printf("   Banking Management System Server (CS-513 System Software)\n");
    printf("   Status: Running on Port %d\n", SERVER_PORT);
    printf("   Features: POSIX System Calls, fcntl Locks, ACID Guarantees\n");
    printf("   Concurrency: Multithreaded with Single Session Enforcement\n");
    printf("=================================================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /* Accept system call */
        int client_sock = accept(g_server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            if (errno == EINTR) continue;
            perror("Accept error");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[Server] Incoming connection accepted from %s:%d\n",
               client_ip, ntohs(client_addr.sin_port));

        /* Dynamically allocate client context with proper size */
        client_conn_t *conn = (client_conn_t *)malloc(sizeof(client_conn_t));
        if (!conn) {
            perror("Memory allocation failure");
            close(client_sock);
            continue;
        }
        conn->sock = client_sock;
        conn->addr = client_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_worker, (void *)conn) != 0) {
            perror("Failed to spawn client worker thread");
            close(client_sock);
            free(conn);
        }
    }

    close(g_server_sock);
    return 0;
}
