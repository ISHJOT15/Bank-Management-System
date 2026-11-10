#include "customer.h"
#include "db.h"
#include "network.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void handle_customer_session(int sock, int customer_id, const char *username) {
    char in_buf[BUFFER_SIZE];
    char out_buf[BUFFER_SIZE];

    while (1) {
        if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) {
            break; /* Client disconnected */
        }

        int choice = atoi(in_buf);
        memset(out_buf, 0, sizeof(out_buf));

        if (choice == 1) {
            /* 1. View Account Balance */
            Customer c;
            if (db_get_customer_by_id(customer_id, &c)) {
                snprintf(out_buf, sizeof(out_buf),
                         "\n========================================\n"
                         " Account ID : %d\n"
                         " Username   : %s\n"
                         " Balance    : $%.2f\n"
                         " Status     : %s\n"
                         "========================================\n\n",
                         c.id, c.username, c.balance, c.is_active ? "Active" : "Deactivated");
            } else {
                snprintf(out_buf, sizeof(out_buf), "Error: Could not retrieve account information.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 2) {
            /* 2. Deposit Money */
            net_send_msg(sock, "PROMPT:Enter deposit amount: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;

            double amount = atof(in_buf);
            double new_bal = 0.0;
            int rc = db_deposit(customer_id, amount, &new_bal);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: Deposit of $%.2f completed! Updated Balance: $%.2f\n", amount, new_bal);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Deposit amount must be positive.\n");
            } else if (rc == -3) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Account is deactivated. Contact branch manager.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Deposit failed due to database error.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 3) {
            /* 3. Withdraw Money */
            net_send_msg(sock, "PROMPT:Enter withdrawal amount: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;

            double amount = atof(in_buf);
            double new_bal = 0.0;
            int rc = db_withdraw(customer_id, amount, &new_bal);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: Withdrawal of $%.2f completed! Updated Balance: $%.2f\n", amount, new_bal);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Withdrawal amount must be positive.\n");
            } else if (rc == -3) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Account is deactivated.\n");
            } else if (rc == -4) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Insufficient funds for this withdrawal.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Withdrawal failed.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 4) {
            /* 4. Transfer Funds */
            net_send_msg(sock, "PROMPT:Enter recipient Account ID: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int to_id = atoi(in_buf);

            net_send_msg(sock, "PROMPT:Enter transfer amount: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            double amount = atof(in_buf);

            double new_bal = 0.0;
            int rc = db_transfer(customer_id, to_id, amount, &new_bal);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: $%.2f transferred to Account %d! Your New Balance: $%.2f\n",
                         amount, to_id, new_bal);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Transfer amount must be positive.\n");
            } else if (rc == -3) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Your account is deactivated.\n");
            } else if (rc == -4) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Insufficient funds to complete transfer.\n");
            } else if (rc == -5) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Cannot transfer funds to your own account.\n");
            } else if (rc == -6) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Sender account not found.\n");
            } else if (rc == -7) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Recipient account ID %d does not exist.\n", to_id);
            } else if (rc == -8) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Recipient account is currently deactivated.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Transaction aborted.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 5) {
            /* 5. Apply for a Loan */
            net_send_msg(sock, "PROMPT:Enter requested loan amount: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            double amount = atof(in_buf);

            net_send_msg(sock, "PROMPT:Enter your monthly income: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            double income = atof(in_buf);

            net_send_msg(sock, "PROMPT:Enter loan purpose: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char purpose[128];
            safe_copy_str(purpose, sizeof(purpose), in_buf);

            int new_lid = 0;
            int rc = db_apply_loan(customer_id, username, amount, purpose, income, &new_lid);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: Loan application submitted successfully! Application ID: %d (Status: PENDING)\n",
                         new_lid);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Amount and income must be greater than zero.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Failed to submit loan application.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 6) {
            /* 6. Change Password */
            net_send_msg(sock, "PROMPT:Enter current password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char old_pass[PASS_LEN];
            safe_copy_str(old_pass, sizeof(old_pass), in_buf);

            net_send_msg(sock, "PROMPT:Enter new password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char new_pass[PASS_LEN];
            safe_copy_str(new_pass, sizeof(new_pass), in_buf);

            int rc = db_change_customer_password(customer_id, old_pass, new_pass);
            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Password changed successfully!\n");
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Current password does not match.\n");
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Password change failed.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 7) {
            /* 7. Adding Feedback */
            net_send_msg(sock, "PROMPT:Enter your feedback/comments: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            in_buf[strcspn(in_buf, "\r\n")] = '\0';

            int fid = db_add_feedback(customer_id, username, in_buf);
            if (fid > 0) {
                snprintf(out_buf, sizeof(out_buf), "SUCCESS: Feedback #%d recorded. Thank you for your feedback!\n", fid);
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Could not submit feedback.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 8) {
            /* 8. View Transaction History */
            char history_buf[BUFFER_SIZE * 4];
            db_get_customer_transactions(customer_id, history_buf, sizeof(history_buf));
            net_send_msg(sock, history_buf);

        } else if (choice == 9) {
            /* 9. Logout */
            net_send_msg(sock, "LOGOUT_OK\n");
            break;

        } else if (choice == 10) {
            /* 10. Exit */
            net_send_msg(sock, "EXIT_OK\n");
            break;

        } else {
            net_send_msg(sock, "ERROR: Invalid selection. Please enter a number between 1 and 10.\n");
        }
    }
}
