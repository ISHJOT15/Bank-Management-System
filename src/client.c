#include "common.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

/* Helper to exchange prompt and response cycles with the server */
static int handle_server_dialog(int sock) {
    char server_reply[BUFFER_SIZE];
    char user_input[BUFFER_SIZE];

    while (1) {
        if (net_recv_msg(sock, server_reply, sizeof(server_reply)) <= 0) {
            printf("\n[Notice] Connection to server lost.\n");
            return -1;
        }

        if (strcmp(server_reply, "LOGOUT_OK\n") == 0 || strcmp(server_reply, "LOGOUT_OK") == 0) {
            printf("\n>>> You have logged out successfully. Returning to Main Menu...\n\n");
            return 1;
        }

        if (strcmp(server_reply, "EXIT_OK\n") == 0 || strcmp(server_reply, "EXIT_OK") == 0) {
            printf("\n>>> Thank you for using Banking Management System. Goodbye!\n");
            exit(0);
        }

        if (strncmp(server_reply, "OUTPUT:", 7) == 0) {
            printf("%s", server_reply + 7);
            fflush(stdout);
            continue; /* Wait for subsequent prompt or final done */
        }

        if (strncmp(server_reply, "PROMPT:", 7) == 0) {
            printf("%s", server_reply + 7);
            fflush(stdout);
            if (!fgets(user_input, sizeof(user_input), stdin)) {
                return -1;
            }
            net_send_msg(sock, user_input);
            continue;
        }

        if (strncmp(server_reply, "DONE:", 5) == 0) {
            printf("%s", server_reply + 5);
            break;
        }

        /* Fallback for un-prefixed messages */
        printf("%s", server_reply);
        break;
    }
    return 0;
}

void show_customer_menu(int sock, int customer_id, const char *username) {
    char choice_str[32];
    while (1) {
        printf("\n============================================\n");
        printf("         CUSTOMER MENU (ID: %d | User: %s)\n", customer_id, username);
        printf("============================================\n");
        printf(" 1. View Account Balance\n");
        printf(" 2. Deposit Money\n");
        printf(" 3. Withdraw Money\n");
        printf(" 4. Transfer Funds\n");
        printf(" 5. Apply for a Loan\n");
        printf(" 6. Change Password\n");
        printf(" 7. Adding Feedback\n");
        printf(" 8. View Transaction History\n");
        printf(" 9. Logout\n");
        printf(" 10. Exit\n");
        printf("============================================\n");
        printf("Enter your choice [1-10]: ");

        if (!fgets(choice_str, sizeof(choice_str), stdin)) break;
        int choice = atoi(choice_str);
        if (choice < 1 || choice > 10) {
            printf("Invalid selection. Please choose an option between 1 and 10.\n");
            continue;
        }

        net_send_msg(sock, choice_str);
        int status = handle_server_dialog(sock);
        if (status != 0) break; /* Logout or disconnect */
    }
}

void show_employee_menu(int sock, int employee_id, const char *username) {
    char choice_str[32];
    while (1) {
        printf("\n============================================\n");
        printf("       BANK EMPLOYEE MENU (ID: %d | %s)\n", employee_id, username);
        printf("============================================\n");
        printf(" 1. Add New Customer\n");
        printf(" 2. Modify Customer Details\n");
        printf(" 3. Process Loan Applications\n");
        printf(" 4. Approve/Reject Loans\n");
        printf(" 5. View Assigned Loan Applications\n");
        printf(" 6. View Customer Transactions (Passbook)\n");
        printf(" 7. Change Password\n");
        printf(" 8. Logout\n");
        printf(" 9. Exit\n");
        printf("============================================\n");
        printf("Enter your choice [1-9]: ");

        if (!fgets(choice_str, sizeof(choice_str), stdin)) break;
        int choice = atoi(choice_str);
        if (choice < 1 || choice > 9) {
            printf("Invalid selection. Please choose an option between 1 and 9.\n");
            continue;
        }

        net_send_msg(sock, choice_str);
        int status = handle_server_dialog(sock);
        if (status != 0) break;
    }
}

void show_manager_menu(int sock, int manager_id, const char *username) {
    char choice_str[32];
    while (1) {
        printf("\n============================================\n");
        printf("          MANAGER MENU (ID: %d | %s)\n", manager_id, username);
        printf("============================================\n");
        printf(" 1. Activate/Deactivate Customer Accounts\n");
        printf(" 2. Assign Loan Application Processes to Employees\n");
        printf(" 3. Review Customer Feedback\n");
        printf(" 4. Change Password\n");
        printf(" 5. Logout\n");
        printf(" 6. Exit\n");
        printf("============================================\n");
        printf("Enter your choice [1-6]: ");

        if (!fgets(choice_str, sizeof(choice_str), stdin)) break;
        int choice = atoi(choice_str);
        if (choice < 1 || choice > 6) {
            printf("Invalid selection. Please choose an option between 1 and 6.\n");
            continue;
        }

        net_send_msg(sock, choice_str);
        int status = handle_server_dialog(sock);
        if (status != 0) break;
    }
}

void show_admin_menu(int sock, int admin_id, const char *username) {
    char choice_str[32];
    while (1) {
        printf("\n============================================\n");
        printf("       ADMINISTRATOR MENU (ID: %d | %s)\n", admin_id, username);
        printf("============================================\n");
        printf(" 1. Add New Bank Employee\n");
        printf(" 2. Modify Customer/Employee Details\n");
        printf(" 3. Manage User Roles\n");
        printf(" 4. Change Password\n");
        printf(" 5. Logout\n");
        printf(" 6. Exit\n");
        printf("============================================\n");
        printf("Enter your choice [1-6]: ");

        if (!fgets(choice_str, sizeof(choice_str), stdin)) break;
        int choice = atoi(choice_str);
        if (choice < 1 || choice > 6) {
            printf("Invalid selection. Please choose an option between 1 and 6.\n");
            continue;
        }

        net_send_msg(sock, choice_str);
        int status = handle_server_dialog(sock);
        if (status != 0) break;
    }
}

int main(int argc, char *argv[]) {
    const char *server_ip = "127.0.0.1";
    int server_port = SERVER_PORT;

    if (argc >= 2) server_ip = argv[1];
    if (argc >= 3) server_port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Failed to create client socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid server address format");
        close(sock);
        return 1;
    }

    printf("Connecting to Banking Management System Server at %s:%d...\n", server_ip, server_port);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to banking server failed");
        close(sock);
        return 1;
    }
    printf("Connected successfully.\n");

    char choice_buf[32];
    char reply_buf[BUFFER_SIZE];
    char input_buf[BUFFER_SIZE];

    while (1) {
        printf("\n====================================================\n");
        printf("          BANKING MANAGEMENT SYSTEM - LOGIN\n");
        printf("====================================================\n");
        printf(" Please select your user role:\n");
        printf(" 1. Administrator\n");
        printf(" 2. Manager\n");
        printf(" 3. Bank Employee\n");
        printf(" 4. Customer\n");
        printf(" 5. Exit\n");
        printf("====================================================\n");
        printf("Enter choice [1-5]: ");

        if (!fgets(choice_buf, sizeof(choice_buf), stdin)) break;
        int role = atoi(choice_buf);

        if (role == 5) {
            net_send_msg(sock, "5\n");
            printf("Exiting application. Goodbye!\n");
            break;
        }

        if (role < 1 || role > 4) {
            printf("Invalid selection. Please choose 1, 2, 3, 4, or 5.\n");
            continue;
        }

        /* Send role selection */
        net_send_msg(sock, choice_buf);

        /* Prompt for Username */
        if (net_recv_msg(sock, reply_buf, sizeof(reply_buf)) <= 0) break;
        if (strncmp(reply_buf, "PROMPT:", 7) == 0) {
            printf("%s", reply_buf + 7);
            fflush(stdout);
            if (!fgets(input_buf, sizeof(input_buf), stdin)) break;
            net_send_msg(sock, input_buf);
        }

        /* Prompt for Password */
        if (net_recv_msg(sock, reply_buf, sizeof(reply_buf)) <= 0) break;
        if (strncmp(reply_buf, "PROMPT:", 7) == 0) {
            printf("%s", reply_buf + 7);
            fflush(stdout);
            if (!fgets(input_buf, sizeof(input_buf), stdin)) break;
            net_send_msg(sock, input_buf);
        }

        /* Authentication Result */
        if (net_recv_msg(sock, reply_buf, sizeof(reply_buf)) <= 0) break;

        if (strncmp(reply_buf, "AUTH_OK:", 8) == 0) {
            int uid = 0;
            char uname[NAME_LEN];
            sscanf(reply_buf + 8, "%d:%63s", &uid, uname);
            printf("\n>>> Authentication Successful! Welcome, %s (ID: %d).\n", uname, uid);

            if (role == ROLE_CUSTOMER) {
                show_customer_menu(sock, uid, uname);
            } else if (role == ROLE_EMPLOYEE) {
                show_employee_menu(sock, uid, uname);
            } else if (role == ROLE_MANAGER) {
                show_manager_menu(sock, uid, uname);
            } else if (role == ROLE_ADMIN) {
                show_admin_menu(sock, uid, uname);
            }
        } else {
            printf("\n%s", reply_buf);
        }
    }

    close(sock);
    return 0;
}
