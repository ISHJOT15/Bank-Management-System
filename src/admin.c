#include "admin.h"
#include "db.h"
#include "network.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void handle_admin_session(int sock, int admin_id, const char *username) {
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
            /* 1. Add New Bank Employee */
            net_send_msg(sock, "PROMPT:Enter new employee username: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char emp_user[NAME_LEN];
            safe_copy_str(emp_user, sizeof(emp_user), in_buf);

            net_send_msg(sock, "PROMPT:Enter employee password: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char emp_pass[PASS_LEN];
            safe_copy_str(emp_pass, sizeof(emp_pass), in_buf);

            net_send_msg(sock, "PROMPT:Enter employee role/designation (e.g., Loan Officer, Cashier): ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            char emp_role[NAME_LEN];
            safe_copy_str(emp_role, sizeof(emp_role), in_buf);

            int new_eid = 0;
            int rc = db_add_employee(emp_user, emp_pass, emp_role, &new_eid);

            if (rc == 1) {
                snprintf(out_buf, sizeof(out_buf),
                         "SUCCESS: Bank Employee registered successfully!\n"
                         "  Employee ID : %d\n"
                         "  Username    : %s\n"
                         "  Designation : %s\n",
                         new_eid, emp_user, emp_role);
            } else if (rc == -2) {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Employee username '%s' already exists.\n", emp_user);
            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Failed to add bank employee.\n");
            }
            net_send_msg(sock, out_buf);

        } else if (choice == 2) {
            /* 2. Modify Customer/Employee Details */
            net_send_msg(sock, "PROMPT:Select User Type to Modify (1. Customer, 2. Employee): ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int utype = atoi(in_buf);

            if (utype == 1) {
                net_send_msg(sock, "PROMPT:Enter Customer Account ID: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                int cid = atoi(in_buf);

                Customer target_c;
                if (!db_get_customer_by_id(cid, &target_c)) {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Customer ID %d does not exist.\n", cid);
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

                int rc = db_modify_customer_details(cid,
                                                    strlen(new_user) > 0 ? new_user : NULL,
                                                    strlen(new_pass) > 0 ? new_pass : NULL);
                if (rc == 1) {
                    snprintf(out_buf, sizeof(out_buf), "SUCCESS: Customer Account %d updated successfully.\n", cid);
                } else if (rc == -2) {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Username is already taken.\n");
                } else {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Failed to update customer details.\n");
                }
                net_send_msg(sock, out_buf);

            } else if (utype == 2) {
                net_send_msg(sock, "PROMPT:Enter Employee ID: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                int eid = atoi(in_buf);

                Employee target_e;
                if (!db_get_employee_by_id(eid, &target_e)) {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Employee ID %d does not exist.\n", eid);
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

                int rc = db_modify_employee_details(eid,
                                                    strlen(new_user) > 0 ? new_user : NULL,
                                                    strlen(new_pass) > 0 ? new_pass : NULL);
                if (rc == 1) {
                    snprintf(out_buf, sizeof(out_buf), "SUCCESS: Employee ID %d updated successfully.\n", eid);
                } else if (rc == -2) {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Username is already taken.\n");
                } else {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Failed to update employee details.\n");
                }
                net_send_msg(sock, out_buf);

            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Invalid user type selected.\n");
                net_send_msg(sock, out_buf);
            }

        } else if (choice == 3) {
            /* 3. Manage User Roles */
            net_send_msg(sock,
                         "OUTPUT:\n--- Manage User Roles Menu ---\n"
                         " 1. View All System Users and Current Roles\n"
                         " 2. Promote Employee to Manager\n"
                         " 3. Reassign Manager to Employee\n"
                         " 4. Remove Employee from System\n"
                         " 5. Remove Manager from System\n");
            net_send_msg(sock, "PROMPT:Select Action [1-5]: ");
            if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
            int sub_choice = atoi(in_buf);

            if (sub_choice == 1) {
                char users_buf[BUFFER_SIZE * 4];
                db_list_all_users_and_roles(users_buf, sizeof(users_buf));
                net_send_msg(sock, users_buf);

            } else if (sub_choice == 2) {
                net_send_msg(sock, "PROMPT:Enter Employee ID to promote to Manager: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                int eid = atoi(in_buf);

                int new_mid = db_change_user_role(eid, ROLE_EMPLOYEE, ROLE_MANAGER, NULL);
                if (new_mid > 0) {
                    snprintf(out_buf, sizeof(out_buf),
                             "SUCCESS: Employee %d promoted to Manager! New Manager ID: %d\n", eid, new_mid);
                } else {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Promotion failed. Check Employee ID.\n");
                }
                net_send_msg(sock, out_buf);

            } else if (sub_choice == 3) {
                net_send_msg(sock, "PROMPT:Enter Manager ID to reassign as Employee: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                int mid = atoi(in_buf);

                net_send_msg(sock, "PROMPT:Enter designation for new employee role: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                in_buf[strcspn(in_buf, "\r\n")] = '\0';

                int new_eid = db_change_user_role(mid, ROLE_MANAGER, ROLE_EMPLOYEE, in_buf);
                if (new_eid > 0) {
                    snprintf(out_buf, sizeof(out_buf),
                             "SUCCESS: Manager %d reassigned as Employee! New Employee ID: %d\n", mid, new_eid);
                } else {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Reassignment failed. Check Manager ID.\n");
                }
                net_send_msg(sock, out_buf);

            } else if (sub_choice == 4) {
                net_send_msg(sock, "PROMPT:Enter Employee ID to remove: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                int eid = atoi(in_buf);

                if (db_remove_user(eid, ROLE_EMPLOYEE)) {
                    snprintf(out_buf, sizeof(out_buf), "SUCCESS: Employee %d removed from system.\n", eid);
                } else {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Employee ID %d not found.\n", eid);
                }
                net_send_msg(sock, out_buf);

            } else if (sub_choice == 5) {
                net_send_msg(sock, "PROMPT:Enter Manager ID to remove: ");
                if (net_recv_msg(sock, in_buf, sizeof(in_buf)) <= 0) break;
                int mid = atoi(in_buf);

                if (db_remove_user(mid, ROLE_MANAGER)) {
                    snprintf(out_buf, sizeof(out_buf), "SUCCESS: Manager %d removed from system.\n", mid);
                } else {
                    snprintf(out_buf, sizeof(out_buf), "ERROR: Manager ID %d not found.\n", mid);
                }
                net_send_msg(sock, out_buf);

            } else {
                snprintf(out_buf, sizeof(out_buf), "ERROR: Invalid action selected.\n");
                net_send_msg(sock, out_buf);
            }

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

            int rc = db_change_admin_password(admin_id, old_pass, new_pass);
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
