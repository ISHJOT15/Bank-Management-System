#include "employee.h"
#include "db.h"
#include "network.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void handle_employee_session(int sock, int employee_id, const char *username) {
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
            /* 1. Add New Customer */
            net_send_msg(sock, "PROMPT:Enter new customer username: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char cust_user[NAME_LEN];
            safe_copy_str(cust_user, sizeof(cust_user), in_buf);

            net_send_msg(sock, "PROMPT:Enter initial password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char cust_pass[PASS_LEN];
            safe_copy_str(cust_pass, sizeof(cust_pass), in_buf);

            net_send_msg(sock, "PROMPT:Enter initial opening deposit: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            double init_bal = atof(in_buf);

            int new_cid = 0;
            int rc = db_add_customer(cust_user, cust_pass, init_bal, &new_cid);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: New Customer Account created!\n"
                         "  Account ID : %d\n"
                         "  Username   : %s\n"
                         "  Balance    : $%.2f\n"
                         "  Status     : Active\n",
                         new_cid, cust_user, init_bal);
            } else if (rc == -3) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Username '%s' already exists.\n", cust_user);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Initial deposit cannot be negative.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Failed to create customer record.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 2) {
            /* 2. Modify Customer Details */
            net_send_msg(sock, "PROMPT:Enter Customer Account ID to modify: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int target_id = atoi(in_buf);

            Customer target_c;
            if (!db_get_customer_by_id(target_id, &target_c)) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Customer ID %d does not exist.\n", target_id);
                net_send_msg(sock, out_buf);
                continue;
            }

            net_send_msg(sock, "PROMPT:Enter new username (or press Enter to keep current): ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char new_user[NAME_LEN];
            safe_copy_str(new_user, sizeof(new_user), in_buf);

            net_send_msg(sock, "PROMPT:Enter new password (or press Enter to keep current): ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char new_pass[PASS_LEN];
            safe_copy_str(new_pass, sizeof(new_pass), in_buf);

            int rc = db_modify_customer_details(target_id,
                                                strlen(new_user) > 0 ? new_user : NULL,
                                                strlen(new_pass) > 0 ? new_pass : NULL);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Customer Account %d updated successfully.\n", target_id);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: New username is already in use by another customer.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Failed to update customer details.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 3) {
            /* 3. Process Loan Applications */
            net_send_msg(sock, "PROMPT:Enter Loan ID to process: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int lid = atoi(in_buf);

            Loan loan;
            if (db_get_loan_details(lid, &loan)) {
                double dti_ratio = (loan.monthly_income > 0) ? (loan.amount / (loan.monthly_income * 12.0)) * 100.0 : 0.0;
                snprintf(out_buf, sizeof(out_buf),
                         "\n=======================================================\n"
                         "              LOAN APPLICATION EVALUATION REPORT\n"
                         "=======================================================\n"
                         " Application ID   : %d\n"
                         " Customer Name    : %s (ID: %d)\n"
                         " Requested Amount : $%.2f\n"
                         " Monthly Income   : $%.2f (Annualized: $%.2f)\n"
                         " Purpose of Loan  : %s\n"
                         " Assigned Officer : Employee %d\n"
                         " Current Status   : %s\n"
                         " DTI Assessment   : %.2f%% of annual income\n"
                         " Recommended Rule : %s\n"
                         "=======================================================\n"
                         "Use Option 4 (Approve/Reject Loans) to render final decision.\n\n",
                         loan.loan_id, loan.customer_username, loan.customer_id,
                         loan.amount, loan.monthly_income, loan.monthly_income * 12.0,
                         loan.purpose, loan.assigned_emp_id, loan.status,
                         dti_ratio, (dti_ratio <= 50.0 ? "ELIGIBLE FOR APPROVAL" : "HIGH DEBT RATIO - SCRUTINY ADVISED"));
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Loan ID %d not found.\n", lid);
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 4) {
            /* 4. Approve/Reject Loans */
            net_send_msg(sock, "PROMPT:Enter Loan ID for decision: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int lid = atoi(in_buf);

            net_send_msg(sock, "PROMPT:Select Decision (1. Approve, 2. Reject): ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int dec = atoi(in_buf);

            if (dec == 1) {
                db_process_loan_decision(lid, employee_id, 1, out_buf, sizeof(out_buf));
            } else if (dec == 2) {
                db_process_loan_decision(lid, employee_id, 0, out_buf, sizeof(out_buf));
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Invalid decision code. Operation cancelled.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 5) {
            /* 5. View Assigned Loan Applications */
            char loan_buf[BUFFER_SIZE * 4];
            db_get_assigned_loans(employee_id, loan_buf, sizeof(loan_buf));
            net_send_msg(sock, loan_buf);

        } else if (choice == 6) {
            /* 6. View Customer Transactions (Passbook Kind of feature) */
            net_send_msg(sock, "PROMPT:Enter Customer Account ID: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int cid = atoi(in_buf);

            char history_buf[BUFFER_SIZE * 4];
            db_get_customer_transactions(cid, history_buf, sizeof(history_buf));
            net_send_msg(sock, history_buf);

        } else if (choice == 7) {
            /* 7. Change Password */
            net_send_msg(sock, "PROMPT:Enter current password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char old_pass[PASS_LEN];
            safe_copy_str(old_pass, sizeof(old_pass), in_buf);

            net_send_msg(sock, "PROMPT:Enter new password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char new_pass[PASS_LEN];
            safe_copy_str(new_pass, sizeof(new_pass), in_buf);

            int rc = db_change_employee_password(employee_id, old_pass, new_pass);
            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Password changed successfully!\n");
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Current password does not match.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Password change failed.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 8) {
            /* 8. Logout */
            net_send_msg(sock, "LOGOUT_OK\n");
            break;

        } else if (choice == 9) {
            /* 9. Exit */
            net_send_msg(sock, "EXIT_OK\n");
            break;

        } else {
            net_send_msg(sock, "ERROR: Invalid selection. Please enter a number between 1 and 9.\n");
        }
    }
}
