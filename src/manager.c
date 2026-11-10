#include "manager.h"
#include "db.h"
#include "network.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void handle_manager_session(int sock, int manager_id, const char *username) {
    (void)username;
    char in_buf[BUFFER_SIZE];
    char out_buf[BUFFER_SIZE];

    while (1) {
        if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) {
            break; /* Disconnected */
        }

        int choice = atoi(in_buf);
        memset(out_buf, 0, sizeof(out_buf));

        if (choice == 1) {
            /* 1. Activate/Deactivate Customer Accounts */
            net_send_msg(sock, "PROMPT:Enter Customer Account ID: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int cid = atoi(in_buf);

            Customer cust;
            if (!db_get_customer_by_id(cid, &cust)) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Customer ID %d does not exist.\n", cid);
                net_send_msg(sock, out_buf);
                continue;
            }

            char prompt_msg[BUFFER_SIZE];
            snprintf(prompt_msg, sizeof(prompt_msg),
                     "PROMPT:Customer '%s' is currently [%s]. Select Action (1. Activate, 2. Deactivate): ",
                     cust.username, cust.is_active ? "ACTIVE" : "DEACTIVATED");
            net_send_msg(sock, prompt_msg);
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;

            int action = atoi(in_buf);
            if (action == 1) {
                db_set_customer_status(cid, 1);
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Customer Account %d has been ACTIVATED.\n", cid);
            } else if (action == 2) {
                db_set_customer_status(cid, 0);
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Customer Account %d has been DEACTIVATED.\n", cid);
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Invalid action selected.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 2) {
            /* 2. Assign Loan Application Processes to Employees */
            char loan_buf[BUFFER_SIZE * 2];
            char emp_buf[BUFFER_SIZE * 2];

            db_get_unassigned_loans(loan_buf, sizeof(loan_buf));
            db_list_all_employees(emp_buf, sizeof(emp_buf));

            char combined[BUFFER_SIZE * 5];
            snprintf(combined, sizeof(combined),
                     "OUTPUT:\n=== UNASSIGNED LOAN APPLICATIONS ===\n%s\n=== AVAILABLE BANK EMPLOYEES ===\n%s\n",
                     loan_buf, emp_buf);
            net_send_msg(sock, combined);

            net_send_msg(sock, "PROMPT:Enter Loan ID to assign: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int lid = atoi(in_buf);

            net_send_msg(sock, "PROMPT:Enter Employee ID to assign to: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int eid = atoi(in_buf);

            int rc = db_assign_loan_to_employee(lid, eid);
            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: Loan ID %d has been successfully assigned to Employee %d for processing.\n",
                         lid, eid);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Employee ID %d does not exist.\n", eid);
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Loan ID %d not found.\n", lid);
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 3) {
            /* 3. Review Customer Feedback */
            char fb_buf[BUFFER_SIZE * 4];
            char out_fb[BUFFER_SIZE * 5];
            db_get_all_feedback(fb_buf, sizeof(fb_buf));
            snprintf(out_fb, sizeof(out_fb), "OUTPUT:%s", fb_buf);
            net_send_msg(sock, out_fb);

            net_send_msg(sock, "PROMPT:Do you wish to clear reviewed feedback? (1. Yes, 2. No): ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            if (atoi(in_buf) == 1) {
                db_clear_feedback();
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Customer feedback register cleared.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "NOTICE: Feedback register preserved.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 4) {
            /* 4. Change Password */
            net_send_msg(sock, "PROMPT:Enter current password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char old_pass[PASS_LEN];
            safe_copy_str(old_pass, sizeof(old_pass), in_buf);

            net_send_msg(sock, "PROMPT:Enter new password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char new_pass[PASS_LEN];
            safe_copy_str(new_pass, sizeof(new_pass), in_buf);

            int rc = db_change_manager_password(manager_id, old_pass, new_pass);
            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Password changed successfully!\n");
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Current password does not match.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Password change failed.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 5) {
            /* 5. Logout */
            net_send_msg(sock, "LOGOUT_OK\n");
            break;

        } else if (choice == 6) {
            /* 6. Exit */
            net_send_msg(sock, "EXIT_OK\n");
            break;

        } else {
            net_send_msg(sock, "ERROR: Invalid selection. Please enter a number between 1 and 6.\n");
        }
    }
}
