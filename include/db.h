#ifndef DB_H
#define DB_H

#include "common.h"

/* System call locking helpers */
int db_lock(int fd, int type);
int db_unlock(int fd);

/* Customer Operations */
int db_auth_customer(const char *username, const char *password, Customer *out_cust);
int db_get_customer_by_id(int id, Customer *out_cust);
int db_get_customer_by_username(const char *username, Customer *out_cust);
int db_add_customer(const char *username, const char *password, double initial_balance, int *out_id);
int db_modify_customer_details(int id, const char *new_username, const char *new_password);
int db_set_customer_status(int id, int is_active);
int db_get_customer_balance(int id, double *out_balance);
int db_deposit(int id, double amount, double *out_new_balance);
int db_withdraw(int id, double amount, double *out_new_balance);
int db_transfer(int from_id, int to_id, double amount, double *out_from_balance);
int db_change_customer_password(int id, const char *old_pass, const char *new_pass);
int db_apply_loan(int customer_id, const char *username, double amount, const char *purpose, double monthly_income, int *out_loan_id);
int db_add_feedback(int customer_id, const char *username, const char *message);
int db_get_customer_transactions(int customer_id, char *out_buf, size_t max_len);

/* Employee Operations */
int db_auth_employee(const char *username, const char *password, Employee *out_emp);
int db_get_employee_by_id(int id, Employee *out_emp);
int db_change_employee_password(int id, const char *old_pass, const char *new_pass);
int db_get_assigned_loans(int emp_id, char *out_buf, size_t max_len);
int db_get_loan_details(int loan_id, Loan *out_loan);
int db_process_loan_decision(int loan_id, int emp_id, int approve, char *out_msg, size_t msg_len);
int db_list_all_employees(char *out_buf, size_t max_len);

/* Manager Operations */
int db_auth_manager(const char *username, const char *password, Manager *out_mgr);
int db_change_manager_password(int id, const char *old_pass, const char *new_pass);
int db_get_unassigned_loans(char *out_buf, size_t max_len);
int db_assign_loan_to_employee(int loan_id, int emp_id);
int db_get_all_feedback(char *out_buf, size_t max_len);
int db_clear_feedback(void);

/* Administrator Operations */
int db_auth_admin(const char *username, const char *password, Admin *out_admin);
int db_change_admin_password(int id, const char *old_pass, const char *new_pass);
int db_add_employee(const char *username, const char *password, const char *role, int *out_id);
int db_modify_employee_details(int id, const char *new_username, const char *new_password);
int db_list_all_users_and_roles(char *out_buf, size_t max_len);
int db_change_user_role(int user_id, int current_role, int target_role, const char *role_title);
int db_remove_user(int user_id, int role);

#endif /* DB_H */
