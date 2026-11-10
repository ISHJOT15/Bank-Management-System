#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096
#define NAME_LEN 64
#define PASS_LEN 64
#define STATUS_LEN 32
#define MAX_RECORDS 500

/* Role Identifiers */
#define ROLE_ADMIN    1
#define ROLE_MANAGER  2
#define ROLE_EMPLOYEE 3
#define ROLE_CUSTOMER 4

/* Database File Locations */
#define CUSTOMER_FILE    "data/customers.txt"
#define EMPLOYEE_FILE    "data/employees.txt"
#define MANAGER_FILE     "data/managers.txt"
#define ADMIN_FILE       "data/admin.txt"
#define LOAN_FILE        "data/loans.txt"
#define TRANSACTION_FILE "data/transactions.txt"
#define FEEDBACK_FILE    "data/feedback.txt"

/* Models */
typedef struct {
    int id;
    char username[NAME_LEN];
    char password[PASS_LEN];
    double balance;
    int is_active; /* 1 = Active, 0 = Deactivated */
} Customer;

typedef struct {
    int id;
    char username[NAME_LEN];
    char password[PASS_LEN];
    char role[NAME_LEN];
} Employee;

typedef struct {
    int id;
    char username[NAME_LEN];
    char password[PASS_LEN];
} Manager;

typedef struct {
    int id;
    char username[NAME_LEN];
    char password[PASS_LEN];
} Admin;

typedef struct {
    int loan_id;
    int customer_id;
    char customer_username[NAME_LEN];
    double amount;
    char purpose[128];
    double monthly_income;
    int assigned_emp_id; /* 0 if unassigned */
    char status[STATUS_LEN]; /* PENDING, ASSIGNED, APPROVED, REJECTED */
} Loan;

typedef struct {
    int trans_id;
    int account_id;
    char username[NAME_LEN];
    char type[32];
    double amount;
    double balance_after;
    char timestamp[32];
} Transaction;

typedef struct {
    int feedback_id;
    int customer_id;
    char customer_username[NAME_LEN];
    char message[256];
    char timestamp[32];
} Feedback;

/* Safe string copy utility preventing GCC truncation warnings */
static inline void safe_copy_str(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) return;
    if (!src) { dest[0] = '\0'; return; }
    size_t i = 0;
    while (i < dest_size - 1 && src[i] != '\0' && src[i] != '\r' && src[i] != '\n') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

#endif /* COMMON_H */
