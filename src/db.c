#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* ========================================================================= */
/* System Call Locking & Synchronization Helpers                             */
/* ========================================================================= */

int db_lock(int fd, int type) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = type;       /* F_RDLCK or F_WRLCK */
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;           /* 0 means lock entire file */
    fl.l_pid = getpid();
    return fcntl(fd, F_SETLKW, &fl); /* Blocking lock system call */
}

int db_unlock(int fd) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = getpid();
    return fcntl(fd, F_SETLK, &fl);  /* Unlock system call */
}

static void safe_truncate(int fd) {
    if (ftruncate(fd, 0) < 0) {
        /* handled */
    }
}

static void safe_write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            break;
        }
        written += (size_t)n;
    }
}

static void get_current_timestamp(char *buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);
}

/* Helper to read all lines from an opened file descriptor using read() system call */
static int read_all_lines(int fd, char lines[][BUFFER_SIZE], int max_lines) {
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) return -1;
    char buffer[BUFFER_SIZE * 2];
    char current_line[BUFFER_SIZE];
    int line_len = 0;
    int line_count = 0;
    ssize_t bytes_read;

    memset(current_line, 0, sizeof(current_line));

    while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            if (c == '\r') continue;
            if (c == '\n') {
                current_line[line_len] = '\0';
                if (line_len > 0 && line_count < max_lines) {
                    snprintf(lines[line_count], BUFFER_SIZE, "%s", current_line);
                    line_count++;
                }
                line_len = 0;
                memset(current_line, 0, sizeof(current_line));
            } else {
                if (line_len < (int)sizeof(current_line) - 2) {
                    current_line[line_len++] = c;
                }
            }
        }
    }

    if (line_len > 0 && line_count < max_lines) {
        current_line[line_len] = '\0';
        snprintf(lines[line_count], BUFFER_SIZE, "%s", current_line);
        line_count++;
    }

    return line_count;
}

/* ========================================================================= */
/* Customer Operations                                                       */
/* ========================================================================= */

static int parse_customer_line(const char *line, Customer *c) {
    char username[NAME_LEN], password[PASS_LEN];
    int id, is_active;
    double balance;
    if (sscanf(line, "%d | %63s | %63s | %lf | %d", &id, username, password, &balance, &is_active) == 5) {
        c->id = id;
        safe_copy_str(c->username, sizeof(c->username), username);
        safe_copy_str(c->password, sizeof(c->password), password);
        c->balance = balance;
        c->is_active = is_active;
        return 1;
    }
    return 0;
}

int db_auth_customer(const char *username, const char *password, Customer *out_cust) {
    int fd = open(CUSTOMER_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Customer c;
        if (parse_customer_line(lines[i], &c)) {
            if (strcmp(c.username, username) == 0 && strcmp(c.password, password) == 0) {
                if (out_cust) *out_cust = c;
                if (!c.is_active) return -2; /* Deactivated account */
                return c.id;                 /* Success */
            }
        }
    }
    return 0; /* Invalid credentials */
}

int db_get_customer_by_id(int id, Customer *out_cust) {
    int fd = open(CUSTOMER_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return 0;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Customer c;
        if (parse_customer_line(lines[i], &c) && c.id == id) {
            if (out_cust) *out_cust = c;
            return 1;
        }
    }
    return 0;
}

int db_get_customer_by_username(const char *username, Customer *out_cust) {
    int fd = open(CUSTOMER_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return 0;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Customer c;
        if (parse_customer_line(lines[i], &c) && strcmp(c.username, username) == 0) {
            if (out_cust) *out_cust = c;
            return 1;
        }
    }
    return 0;
}

/* Logging transactions helper using write() and fsync() */
static int log_transaction_internal(int account_id, const char *username, const char *type, double amount, double balance_after) {
    int fd = open(TRANSACTION_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    int max_tid = 9000;
    for (int i = 0; i < count; i++) {
        int tid;
        if (sscanf(lines[i], "%d |", &tid) == 1 && tid > max_tid) {
            max_tid = tid;
        }
    }
    int new_tid = max_tid + 1;

    char timestamp[32];
    get_current_timestamp(timestamp, sizeof(timestamp));

    char record[BUFFER_SIZE];
    int len = snprintf(record, sizeof(record), "%d | %d | %s | %s | %.2f | %.2f | %s\n",
                       new_tid, account_id, username, type, amount, balance_after, timestamp);

    lseek(fd, 0, SEEK_END);
    safe_write_all(fd, record, len);
    fsync(fd); /* ACID Durability guarantee */

    db_unlock(fd);
    close(fd);
    return new_tid;
}

int db_add_customer(const char *username, const char *password, double initial_balance, int *out_id) {
    if (initial_balance < 0) return -2;

    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    int max_id = 1000;
    for (int i = 0; i < count; i++) {
        Customer c;
        if (parse_customer_line(lines[i], &c)) {
            if (strcmp(c.username, username) == 0) {
                db_unlock(fd);
                close(fd);
                return -3; /* Username already exists */
            }
            if (c.id > max_id) max_id = c.id;
        }
    }

    int new_id = max_id + 1;
    char record[BUFFER_SIZE];
    int len = snprintf(record, sizeof(record), "%d | %s | %s | %.2f | 1\n",
                       new_id, username, password, initial_balance);

    lseek(fd, 0, SEEK_END);
    safe_write_all(fd, record, len);
    fsync(fd); /* ACID Durability */

    db_unlock(fd);
    close(fd);

    if (out_id) *out_id = new_id;

    /* Log initial deposit */
    log_transaction_internal(new_id, username, "INITIAL_DEPOSIT", initial_balance, initial_balance);
    return 1;
}

int db_modify_customer_details(int id, const char *new_username, const char *new_password) {
    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Customer list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_customer_line(lines[i], &list[i]);
        if (list[i].id == id) {
            found_idx = i;
        } else if (new_username && strlen(new_username) > 0 && strcmp(list[i].username, new_username) == 0) {
            db_unlock(fd);
            close(fd);
            return -2; /* Username taken by another customer */
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0; /* Customer not found */
    }

    if (new_username && strlen(new_username) > 0) {
        strncpy(list[found_idx].username, new_username, sizeof(list[found_idx].username) - 1);
    }
    if (new_password && strlen(new_password) > 0) {
        strncpy(list[found_idx].password, new_password, sizeof(list[found_idx].password) - 1);
    }

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                           list[i].id, list[i].username, list[i].password, list[i].balance, list[i].is_active);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_set_customer_status(int id, int is_active) {
    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Customer list[MAX_RECORDS];
    int found = 0;

    for (int i = 0; i < count; i++) {
        parse_customer_line(lines[i], &list[i]);
        if (list[i].id == id) {
            list[i].is_active = (is_active ? 1 : 0);
            found = 1;
        }
    }

    if (!found) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                           list[i].id, list[i].username, list[i].password, list[i].balance, list[i].is_active);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_get_customer_balance(int id, double *out_balance) {
    Customer c;
    if (db_get_customer_by_id(id, &c)) {
        if (out_balance) *out_balance = c.balance;
        return 1;
    }
    return 0;
}

int db_deposit(int id, double amount, double *out_new_balance) {
    if (amount <= 0) return -2; /* Invalid amount */

    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Customer list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_customer_line(lines[i], &list[i]);
        if (list[i].id == id) {
            found_idx = i;
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0; /* Customer not found */
    }

    if (!list[found_idx].is_active) {
        db_unlock(fd);
        close(fd);
        return -3; /* Account deactivated */
    }

    list[found_idx].balance += amount;
    double new_bal = list[found_idx].balance;

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                           list[i].id, list[i].username, list[i].password, list[i].balance, list[i].is_active);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);

    log_transaction_internal(id, list[found_idx].username, "DEPOSIT", amount, new_bal);
    if (out_new_balance) *out_new_balance = new_bal;
    return 1;
}

int db_withdraw(int id, double amount, double *out_new_balance) {
    if (amount <= 0) return -2; /* Invalid amount */

    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Customer list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_customer_line(lines[i], &list[i]);
        if (list[i].id == id) {
            found_idx = i;
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    if (!list[found_idx].is_active) {
        db_unlock(fd);
        close(fd);
        return -3; /* Deactivated */
    }

    if (list[found_idx].balance < amount) {
        db_unlock(fd);
        close(fd);
        return -4; /* Insufficient funds */
    }

    list[found_idx].balance -= amount;
    double new_bal = list[found_idx].balance;

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                           list[i].id, list[i].username, list[i].password, list[i].balance, list[i].is_active);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);

    log_transaction_internal(id, list[found_idx].username, "WITHDRAWAL", amount, new_bal);
    if (out_new_balance) *out_new_balance = new_bal;
    return 1;
}

/* Atomic Fund Transfer with Deadlock Avoidance and Full Consistency */
int db_transfer(int from_id, int to_id, double amount, double *out_from_balance) {
    if (amount <= 0) return -2;
    if (from_id == to_id) return -5; /* Cannot transfer to self */

    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    /* Exclusive write lock guarantees Isolation */
    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Customer list[MAX_RECORDS];
    int from_idx = -1;
    int to_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_customer_line(lines[i], &list[i]);
        if (list[i].id == from_id) from_idx = i;
        if (list[i].id == to_id) to_idx = i;
    }

    if (from_idx == -1) {
        db_unlock(fd);
        close(fd);
        return -6; /* Sender account not found */
    }
    if (to_idx == -1) {
        db_unlock(fd);
        close(fd);
        return -7; /* Recipient account not found */
    }
    if (!list[from_idx].is_active) {
        db_unlock(fd);
        close(fd);
        return -3; /* Sender account deactivated */
    }
    if (!list[to_idx].is_active) {
        db_unlock(fd);
        close(fd);
        return -8; /* Recipient account deactivated */
    }
    if (list[from_idx].balance < amount) {
        db_unlock(fd);
        close(fd);
        return -4; /* Insufficient funds */
    }

    /* ACID Atomicity: Apply debits and credits simultaneously */
    list[from_idx].balance -= amount;
    list[to_idx].balance += amount;

    double from_new_bal = list[from_idx].balance;
    double to_new_bal = list[to_idx].balance;

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                           list[i].id, list[i].username, list[i].password, list[i].balance, list[i].is_active);
        safe_write_all(fd, buf, len);
    }
    fsync(fd); /* ACID Durability */

    db_unlock(fd);
    close(fd);

    /* Log dual transactions */
    log_transaction_internal(from_id, list[from_idx].username, "TRANSFER_DEBIT", amount, from_new_bal);
    log_transaction_internal(to_id, list[to_idx].username, "TRANSFER_CREDIT", amount, to_new_bal);

    if (out_from_balance) *out_from_balance = from_new_bal;
    return 1;
}

int db_change_customer_password(int id, const char *old_pass, const char *new_pass) {
    int fd = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Customer list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_customer_line(lines[i], &list[i]);
        if (list[i].id == id) {
            found_idx = i;
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    if (strcmp(list[found_idx].password, old_pass) != 0) {
        db_unlock(fd);
        close(fd);
        return -2; /* Old password mismatch */
    }

    strncpy(list[found_idx].password, new_pass, sizeof(list[found_idx].password) - 1);

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                           list[i].id, list[i].username, list[i].password, list[i].balance, list[i].is_active);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_apply_loan(int customer_id, const char *username, double amount, const char *purpose, double monthly_income, int *out_loan_id) {
    if (amount <= 0 || monthly_income <= 0) return -2;

    int fd = open(LOAN_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    int max_lid = 5000;
    for (int i = 0; i < count; i++) {
        int lid;
        if (sscanf(lines[i], "%d |", &lid) == 1 && lid > max_lid) {
            max_lid = lid;
        }
    }

    int new_lid = max_lid + 1;
    char record[BUFFER_SIZE];
    int len = snprintf(record, sizeof(record), "%d | %d | %s | %.2f | %s | %.2f | 0 | PENDING\n",
                       new_lid, customer_id, username, amount, purpose, monthly_income);

    lseek(fd, 0, SEEK_END);
    safe_write_all(fd, record, len);
    fsync(fd);

    db_unlock(fd);
    close(fd);

    if (out_loan_id) *out_loan_id = new_lid;
    return 1;
}

int db_add_feedback(int customer_id, const char *username, const char *message) {
    int fd = open(FEEDBACK_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    int max_fid = 8000;
    for (int i = 0; i < count; i++) {
        int fid;
        if (sscanf(lines[i], "%d |", &fid) == 1 && fid > max_fid) {
            max_fid = fid;
        }
    }
    int new_fid = max_fid + 1;

    char timestamp[32];
    get_current_timestamp(timestamp, sizeof(timestamp));

    char record[BUFFER_SIZE];
    int len = snprintf(record, sizeof(record), "%d | %d | %s | %s | %s\n",
                       new_fid, customer_id, username, message, timestamp);

    lseek(fd, 0, SEEK_END);
    safe_write_all(fd, record, len);
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return new_fid;
}

int db_get_customer_transactions(int customer_id, char *out_buf, size_t max_len) {
    int fd = open(TRANSACTION_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    out_buf[0] = '\0';
    size_t cur_len = 0;
    int found_count = 0;

    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                        "----------------------------------------------------------------------------------------\n"
                        " %-8s | %-16s | %-12s | %-12s | %-19s\n"
                        "----------------------------------------------------------------------------------------\n",
                        "TXN ID", "TYPE", "AMOUNT", "BALANCE AFTER", "TIMESTAMP");

    for (int i = 0; i < count; i++) {
        int tid, acid;
        char uname[NAME_LEN], type[32], tstamp[32];
        double amt, bal_after;
        if (sscanf(lines[i], "%d | %d | %63s | %31s | %lf | %lf | %31[^\n]",
                   &tid, &acid, uname, type, &amt, &bal_after, tstamp) >= 6) {
            if (acid == customer_id) {
                found_count++;
                if (cur_len + 150 < max_len) {
                    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                        " %-8d | %-16s | %12.2f | %12.2f | %-19s\n",
                                        tid, type, amt, bal_after, tstamp);
                }
            }
        }
    }

    if (found_count == 0) {
        snprintf(out_buf, max_len, "No transaction history recorded for Account ID %d.\n", customer_id);
    } else {
        snprintf(out_buf + cur_len, max_len - cur_len,
                 "----------------------------------------------------------------------------------------\n"
                 " Total Transactions: %d\n", found_count);
    }
    return found_count;
}

/* ========================================================================= */
/* Employee Operations                                                       */
/* ========================================================================= */

static int parse_employee_line(const char *line, Employee *e) {
    char username[NAME_LEN], password[PASS_LEN], role[NAME_LEN];
    int id;
    if (sscanf(line, "%d | %63s | %63s | %63[^\n]", &id, username, password, role) >= 3) {
        e->id = id;
        safe_copy_str(e->username, sizeof(e->username), username);
        safe_copy_str(e->password, sizeof(e->password), password);
        if (strlen(role) > 0) {
            safe_copy_str(e->role, sizeof(e->role), role);
        } else {
            safe_copy_str(e->role, sizeof(e->role), "Employee");
        }
        return 1;
    }
    return 0;
}

int db_auth_employee(const char *username, const char *password, Employee *out_emp) {
    int fd = open(EMPLOYEE_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Employee e;
        if (parse_employee_line(lines[i], &e)) {
            if (strcmp(e.username, username) == 0 && strcmp(e.password, password) == 0) {
                if (out_emp) *out_emp = e;
                return e.id;
            }
        }
    }
    return 0;
}

int db_get_employee_by_id(int id, Employee *out_emp) {
    int fd = open(EMPLOYEE_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return 0;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Employee e;
        if (parse_employee_line(lines[i], &e) && e.id == id) {
            if (out_emp) *out_emp = e;
            return 1;
        }
    }
    return 0;
}

int db_change_employee_password(int id, const char *old_pass, const char *new_pass) {
    int fd = open(EMPLOYEE_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Employee list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_employee_line(lines[i], &list[i]);
        if (list[i].id == id) found_idx = i;
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    if (strcmp(list[found_idx].password, old_pass) != 0) {
        db_unlock(fd);
        close(fd);
        return -2; /* Password mismatch */
    }

    strncpy(list[found_idx].password, new_pass, sizeof(list[found_idx].password) - 1);

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %s\n",
                           list[i].id, list[i].username, list[i].password, list[i].role);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

static int parse_loan_line(const char *line, Loan *l) {
    char uname[NAME_LEN], purpose[128], status[STATUS_LEN];
    int lid, cid, emp_id;
    double amt, income;
    if (sscanf(line, "%d | %d | %63s | %lf | %127[^|] | %lf | %d | %31s",
               &lid, &cid, uname, &amt, purpose, &income, &emp_id, status) >= 7) {
        l->loan_id = lid;
        l->customer_id = cid;
        safe_copy_str(l->customer_username, sizeof(l->customer_username), uname);
        l->amount = amt;
        /* Trim whitespace from purpose */
        char *p = purpose;
        while (*p == ' ') p++;
        size_t plen = strlen(p);
        while (plen > 0 && p[plen - 1] == ' ') p[--plen] = '\0';
        safe_copy_str(l->purpose, sizeof(l->purpose), p);
        l->monthly_income = income;
        l->assigned_emp_id = emp_id;
        safe_copy_str(l->status, sizeof(l->status), status);
        return 1;
    }
    return 0;
}

int db_get_assigned_loans(int emp_id, char *out_buf, size_t max_len) {
    int fd = open(LOAN_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    out_buf[0] = '\0';
    size_t cur_len = 0;
    int found_count = 0;

    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                        "-------------------------------------------------------------------------------------------------\n"
                        " %-8s | %-14s | %-12s | %-12s | %-10s | %-24s\n"
                        "-------------------------------------------------------------------------------------------------\n",
                        "LOAN ID", "CUSTOMER", "AMOUNT", "INCOME", "STATUS", "PURPOSE");

    for (int i = 0; i < count; i++) {
        Loan l;
        if (parse_loan_line(lines[i], &l) && l.assigned_emp_id == emp_id) {
            found_count++;
            if (cur_len + 150 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-14s | %12.2f | %12.2f | %-10s | %-24s\n",
                                    l.loan_id, l.customer_username, l.amount, l.monthly_income, l.status, l.purpose);
            }
        }
    }

    if (found_count == 0) {
        snprintf(out_buf, max_len, "No loan applications currently assigned to Employee ID %d.\n", emp_id);
    } else {
        snprintf(out_buf + cur_len, max_len - cur_len,
                 "-------------------------------------------------------------------------------------------------\n"
                 " Total Assigned Loans: %d\n", found_count);
    }
    return found_count;
}

int db_get_loan_details(int loan_id, Loan *out_loan) {
    int fd = open(LOAN_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return 0;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Loan l;
        if (parse_loan_line(lines[i], &l) && l.loan_id == loan_id) {
            if (out_loan) *out_loan = l;
            return 1;
        }
    }
    return 0;
}

/* Atomically Process Loan Application Decision (Approve or Reject) */
int db_process_loan_decision(int loan_id, int emp_id, int approve, char *out_msg, size_t msg_len) {
    int fd_loan = open(LOAN_FILE, O_RDWR | O_CREAT, 0644);
    if (fd_loan < 0) return -1;

    db_lock(fd_loan, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd_loan, lines, MAX_RECORDS);

    Loan list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_loan_line(lines[i], &list[i]);
        if (list[i].loan_id == loan_id) {
            found_idx = i;
        }
    }

    if (found_idx == -1) {
        db_unlock(fd_loan);
        close(fd_loan);
        snprintf(out_msg, msg_len, "Error: Loan ID %d not found.\n", loan_id);
        return 0;
    }

    if (list[found_idx].assigned_emp_id != emp_id && list[found_idx].assigned_emp_id != 0) {
        db_unlock(fd_loan);
        close(fd_loan);
        snprintf(out_msg, msg_len, "Error: Loan ID %d is assigned to Employee %d (not you).\n",
                 loan_id, list[found_idx].assigned_emp_id);
        return -2;
    }

    if (strcmp(list[found_idx].status, "PENDING") != 0 && strcmp(list[found_idx].status, "ASSIGNED") != 0) {
        db_unlock(fd_loan);
        close(fd_loan);
        snprintf(out_msg, msg_len, "Error: Loan ID %d has already been processed (Status: %s).\n",
                 loan_id, list[found_idx].status);
        return -3;
    }

    if (approve) {
        strncpy(list[found_idx].status, "APPROVED", sizeof(list[found_idx].status) - 1);
        list[found_idx].assigned_emp_id = emp_id;

        /* Atomically Credit Customer Balance and Log Transaction */
        int fd_cust = open(CUSTOMER_FILE, O_RDWR | O_CREAT, 0644);
        if (fd_cust >= 0) {
            db_lock(fd_cust, F_WRLCK);
            char c_lines[MAX_RECORDS][BUFFER_SIZE];
            int c_count = read_all_lines(fd_cust, c_lines, MAX_RECORDS);
            Customer c_list[MAX_RECORDS];
            int c_idx = -1;

            for (int j = 0; j < c_count; j++) {
                parse_customer_line(c_lines[j], &c_list[j]);
                if (c_list[j].id == list[found_idx].customer_id) {
                    c_idx = j;
                }
            }

            if (c_idx != -1) {
                c_list[c_idx].balance += list[found_idx].amount;
                double new_balance = c_list[c_idx].balance;

                lseek(fd_cust, 0, SEEK_SET);
                safe_truncate(fd_cust);
                for (int j = 0; j < c_count; j++) {
                    char buf[BUFFER_SIZE];
                    int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %.2f | %d\n",
                                       c_list[j].id, c_list[j].username, c_list[j].password,
                                       c_list[j].balance, c_list[j].is_active);
                    safe_write_all(fd_cust, buf, len);
                }
                fsync(fd_cust);
                log_transaction_internal(c_list[c_idx].id, c_list[c_idx].username,
                                         "LOAN_DISBURSED", list[found_idx].amount, new_balance);
            }
            db_unlock(fd_cust);
            close(fd_cust);
        }

        snprintf(out_msg, msg_len, "Success: Loan ID %d approved! Disbursed $%.2f to Customer %s.\n",
                 loan_id, list[found_idx].amount, list[found_idx].customer_username);
    } else {
        strncpy(list[found_idx].status, "REJECTED", sizeof(list[found_idx].status) - 1);
        list[found_idx].assigned_emp_id = emp_id;
        snprintf(out_msg, msg_len, "Notice: Loan ID %d marked as REJECTED.\n", loan_id);
    }

    /* Save updated loan table */
    lseek(fd_loan, 0, SEEK_SET);
    safe_truncate(fd_loan);
    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %d | %s | %.2f | %s | %.2f | %d | %s\n",
                           list[i].loan_id, list[i].customer_id, list[i].customer_username,
                           list[i].amount, list[i].purpose, list[i].monthly_income,
                           list[i].assigned_emp_id, list[i].status);
        safe_write_all(fd_loan, buf, len);
    }
    fsync(fd_loan);

    db_unlock(fd_loan);
    close(fd_loan);
    return 1;
}

int db_list_all_employees(char *out_buf, size_t max_len) {
    int fd = open(EMPLOYEE_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    out_buf[0] = '\0';
    size_t cur_len = 0;
    int found_count = 0;

    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                        "----------------------------------------------------------\n"
                        " %-8s | %-16s | %-24s\n"
                        "----------------------------------------------------------\n",
                        "EMP ID", "USERNAME", "DESIGNATION");

    for (int i = 0; i < count; i++) {
        Employee e;
        if (parse_employee_line(lines[i], &e)) {
            found_count++;
            if (cur_len + 100 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-16s | %-24s\n",
                                    e.id, e.username, e.role);
            }
        }
    }

    if (found_count == 0) {
        snprintf(out_buf, max_len, "No bank employees found.\n");
    } else {
        snprintf(out_buf + cur_len, max_len - cur_len,
                 "----------------------------------------------------------\n"
                 " Total Employees: %d\n", found_count);
    }
    return found_count;
}

/* ========================================================================= */
/* Manager Operations                                                        */
/* ========================================================================= */

static int parse_manager_line(const char *line, Manager *m) {
    char username[NAME_LEN], password[PASS_LEN];
    int id;
    if (sscanf(line, "%d | %63s | %63s", &id, username, password) == 3) {
        m->id = id;
        safe_copy_str(m->username, sizeof(m->username), username);
        safe_copy_str(m->password, sizeof(m->password), password);
        return 1;
    }
    return 0;
}

int db_auth_manager(const char *username, const char *password, Manager *out_mgr) {
    int fd = open(MANAGER_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Manager m;
        if (parse_manager_line(lines[i], &m)) {
            if (strcmp(m.username, username) == 0 && strcmp(m.password, password) == 0) {
                if (out_mgr) *out_mgr = m;
                return m.id;
            }
        }
    }
    return 0;
}

int db_change_manager_password(int id, const char *old_pass, const char *new_pass) {
    int fd = open(MANAGER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Manager list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_manager_line(lines[i], &list[i]);
        if (list[i].id == id) found_idx = i;
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    if (strcmp(list[found_idx].password, old_pass) != 0) {
        db_unlock(fd);
        close(fd);
        return -2;
    }

    strncpy(list[found_idx].password, new_pass, sizeof(list[found_idx].password) - 1);

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s\n",
                           list[i].id, list[i].username, list[i].password);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_get_unassigned_loans(char *out_buf, size_t max_len) {
    int fd = open(LOAN_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    out_buf[0] = '\0';
    size_t cur_len = 0;
    int found_count = 0;

    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                        "-------------------------------------------------------------------------------------------------\n"
                        " %-8s | %-14s | %-12s | %-12s | %-10s | %-24s\n"
                        "-------------------------------------------------------------------------------------------------\n",
                        "LOAN ID", "CUSTOMER", "AMOUNT", "INCOME", "STATUS", "PURPOSE");

    for (int i = 0; i < count; i++) {
        Loan l;
        if (parse_loan_line(lines[i], &l) && (l.assigned_emp_id == 0 || strcmp(l.status, "PENDING") == 0)) {
            found_count++;
            if (cur_len + 150 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-14s | %12.2f | %12.2f | %-10s | %-24s\n",
                                    l.loan_id, l.customer_username, l.amount, l.monthly_income, l.status, l.purpose);
            }
        }
    }

    if (found_count == 0) {
        snprintf(out_buf, max_len, "All loan applications have currently been assigned or processed.\n");
    } else {
        snprintf(out_buf + cur_len, max_len - cur_len,
                 "-------------------------------------------------------------------------------------------------\n"
                 " Total Unassigned Pending Loans: %d\n", found_count);
    }
    return found_count;
}

int db_assign_loan_to_employee(int loan_id, int emp_id) {
    /* Verify employee exists */
    Employee emp;
    if (!db_get_employee_by_id(emp_id, &emp)) {
        return -2; /* Employee ID does not exist */
    }

    int fd = open(LOAN_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Loan list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_loan_line(lines[i], &list[i]);
        if (list[i].loan_id == loan_id) {
            found_idx = i;
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0; /* Loan ID not found */
    }

    list[found_idx].assigned_emp_id = emp_id;
    strncpy(list[found_idx].status, "ASSIGNED", sizeof(list[found_idx].status) - 1);

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %d | %s | %.2f | %s | %.2f | %d | %s\n",
                           list[i].loan_id, list[i].customer_id, list[i].customer_username,
                           list[i].amount, list[i].purpose, list[i].monthly_income,
                           list[i].assigned_emp_id, list[i].status);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_get_all_feedback(char *out_buf, size_t max_len) {
    int fd = open(FEEDBACK_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    out_buf[0] = '\0';
    size_t cur_len = 0;
    int found_count = 0;

    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                        "========================================================================================\n"
                        "                               CUSTOMER FEEDBACK REGISTER\n"
                        "========================================================================================\n");

    for (int i = 0; i < count; i++) {
        int fid, cid;
        char uname[NAME_LEN], msg[256], tstamp[32];
        if (sscanf(lines[i], "%d | %d | %63s | %255[^|] | %31[^\n]",
                   &fid, &cid, uname, msg, tstamp) >= 4) {
            found_count++;
            if (cur_len + 350 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " [%d] Date: %s | Customer: %s (ID: %d)\n"
                                    "     Message: %s\n"
                                    "----------------------------------------------------------------------------------------\n",
                                    fid, tstamp, uname, cid, msg);
            }
        }
    }

    if (found_count == 0) {
        snprintf(out_buf, max_len, "No customer feedback entries available.\n");
    } else {
        snprintf(out_buf + cur_len, max_len - cur_len,
                 " Total Feedback Submissions: %d\n", found_count);
    }
    return found_count;
}

int db_clear_feedback(void) {
    int fd = open(FEEDBACK_FILE, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) return -1;
    db_lock(fd, F_WRLCK);
    fsync(fd);
    db_unlock(fd);
    close(fd);
    return 1;
}

/* ========================================================================= */
/* Administrator Operations                                                  */
/* ========================================================================= */

static int parse_admin_line(const char *line, Admin *a) {
    char username[NAME_LEN], password[PASS_LEN];
    int id;
    if (sscanf(line, "%d | %63s | %63s", &id, username, password) == 3) {
        a->id = id;
        safe_copy_str(a->username, sizeof(a->username), username);
        safe_copy_str(a->password, sizeof(a->password), password);
        return 1;
    }
    return 0;
}

int db_auth_admin(const char *username, const char *password, Admin *out_admin) {
    int fd = open(ADMIN_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_RDLCK);
    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);
    db_unlock(fd);
    close(fd);

    for (int i = 0; i < count; i++) {
        Admin a;
        if (parse_admin_line(lines[i], &a)) {
            if (strcmp(a.username, username) == 0 && strcmp(a.password, password) == 0) {
                if (out_admin) *out_admin = a;
                return a.id;
            }
        }
    }
    return 0;
}

int db_change_admin_password(int id, const char *old_pass, const char *new_pass) {
    int fd = open(ADMIN_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Admin list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_admin_line(lines[i], &list[i]);
        if (list[i].id == id) found_idx = i;
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    if (strcmp(list[found_idx].password, old_pass) != 0) {
        db_unlock(fd);
        close(fd);
        return -2;
    }

    strncpy(list[found_idx].password, new_pass, sizeof(list[found_idx].password) - 1);

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s\n",
                           list[i].id, list[i].username, list[i].password);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_add_employee(const char *username, const char *password, const char *role, int *out_id) {
    int fd = open(EMPLOYEE_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    int max_id = 2000;
    for (int i = 0; i < count; i++) {
        Employee e;
        if (parse_employee_line(lines[i], &e)) {
            if (strcmp(e.username, username) == 0) {
                db_unlock(fd);
                close(fd);
                return -2; /* Username exists */
            }
            if (e.id > max_id) max_id = e.id;
        }
    }

    int new_id = max_id + 1;
    char record[BUFFER_SIZE];
    int len = snprintf(record, sizeof(record), "%d | %s | %s | %s\n",
                       new_id, username, password, (role && strlen(role) > 0 ? role : "Employee"));

    lseek(fd, 0, SEEK_END);
    safe_write_all(fd, record, len);
    fsync(fd);

    db_unlock(fd);
    close(fd);

    if (out_id) *out_id = new_id;
    return 1;
}

int db_modify_employee_details(int id, const char *new_username, const char *new_password) {
    int fd = open(EMPLOYEE_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    Employee list[MAX_RECORDS];
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        parse_employee_line(lines[i], &list[i]);
        if (list[i].id == id) {
            found_idx = i;
        } else if (new_username && strlen(new_username) > 0 && strcmp(list[i].username, new_username) == 0) {
            db_unlock(fd);
            close(fd);
            return -2; /* Username taken */
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    if (new_username && strlen(new_username) > 0) {
        strncpy(list[found_idx].username, new_username, sizeof(list[found_idx].username) - 1);
    }
    if (new_password && strlen(new_password) > 0) {
        strncpy(list[found_idx].password, new_password, sizeof(list[found_idx].password) - 1);
    }

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s | %s\n",
                           list[i].id, list[i].username, list[i].password, list[i].role);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}

int db_list_all_users_and_roles(char *out_buf, size_t max_len) {
    out_buf[0] = '\0';
    size_t cur_len = 0;

    cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                        "========================================================================================\n"
                        "                               SYSTEM USERS & ROLE ASSIGNMENTS\n"
                        "========================================================================================\n"
                        " %-8s | %-16s | %-16s | %-20s\n"
                        "----------------------------------------------------------------------------------------\n",
                        "USER ID", "USERNAME", "PRIMARY ROLE", "DETAILS/STATUS");

    /* 1. Admins */
    int fd_adm = open(ADMIN_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd_adm >= 0) {
        db_lock(fd_adm, F_RDLCK);
        char lines[MAX_RECORDS][BUFFER_SIZE];
        int count = read_all_lines(fd_adm, lines, MAX_RECORDS);
        db_unlock(fd_adm);
        close(fd_adm);

        for (int i = 0; i < count; i++) {
            Admin a;
            if (parse_admin_line(lines[i], &a) && cur_len + 100 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-16s | %-16s | %-20s\n",
                                    a.id, a.username, "Administrator", "Superuser Access");
            }
        }
    }

    /* 2. Managers */
    int fd_mgr = open(MANAGER_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd_mgr >= 0) {
        db_lock(fd_mgr, F_RDLCK);
        char lines[MAX_RECORDS][BUFFER_SIZE];
        int count = read_all_lines(fd_mgr, lines, MAX_RECORDS);
        db_unlock(fd_mgr);
        close(fd_mgr);

        for (int i = 0; i < count; i++) {
            Manager m;
            if (parse_manager_line(lines[i], &m) && cur_len + 100 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-16s | %-16s | %-20s\n",
                                    m.id, m.username, "Manager", "Branch Management");
            }
        }
    }

    /* 3. Employees */
    int fd_emp = open(EMPLOYEE_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd_emp >= 0) {
        db_lock(fd_emp, F_RDLCK);
        char lines[MAX_RECORDS][BUFFER_SIZE];
        int count = read_all_lines(fd_emp, lines, MAX_RECORDS);
        db_unlock(fd_emp);
        close(fd_emp);

        for (int i = 0; i < count; i++) {
            Employee e;
            if (parse_employee_line(lines[i], &e) && cur_len + 100 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-16s | %-16s | %-20s\n",
                                    e.id, e.username, "Bank Employee", e.role);
            }
        }
    }

    /* 4. Customers */
    int fd_cust = open(CUSTOMER_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd_cust >= 0) {
        db_lock(fd_cust, F_RDLCK);
        char lines[MAX_RECORDS][BUFFER_SIZE];
        int count = read_all_lines(fd_cust, lines, MAX_RECORDS);
        db_unlock(fd_cust);
        close(fd_cust);

        for (int i = 0; i < count; i++) {
            Customer c;
            if (parse_customer_line(lines[i], &c) && cur_len + 100 < max_len) {
                cur_len += snprintf(out_buf + cur_len, max_len - cur_len,
                                    " %-8d | %-16s | %-16s | %s ($%.2f)\n",
                                    c.id, c.username, "Customer",
                                    c.is_active ? "Active" : "Deactivated", c.balance);
            }
        }
    }

    snprintf(out_buf + cur_len, max_len - cur_len,
             "========================================================================================\n");
    return 1;
}

int db_change_user_role(int user_id, int current_role, int target_role, const char *role_title) {
    if (current_role == target_role) return -2;

    /* Promoting Employee to Manager */
    if (current_role == ROLE_EMPLOYEE && target_role == ROLE_MANAGER) {
        Employee emp;
        if (!db_get_employee_by_id(user_id, &emp)) return 0;

        /* Remove from employees */
        db_remove_user(user_id, ROLE_EMPLOYEE);

        /* Add to managers */
        int fd_mgr = open(MANAGER_FILE, O_RDWR | O_CREAT, 0644);
        if (fd_mgr < 0) return -1;
        db_lock(fd_mgr, F_WRLCK);

        char lines[MAX_RECORDS][BUFFER_SIZE];
        int count = read_all_lines(fd_mgr, lines, MAX_RECORDS);
        int max_mid = 3000;
        for (int i = 0; i < count; i++) {
            int mid;
            if (sscanf(lines[i], "%d |", &mid) == 1 && mid > max_mid) max_mid = mid;
        }
        int new_mid = max_mid + 1;

        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%d | %s | %s\n", new_mid, emp.username, emp.password);
        lseek(fd_mgr, 0, SEEK_END);
        safe_write_all(fd_mgr, buf, len);
        fsync(fd_mgr);
        db_unlock(fd_mgr);
        close(fd_mgr);
        return new_mid;
    }

    /* Demoting Manager to Employee */
    if (current_role == ROLE_MANAGER && target_role == ROLE_EMPLOYEE) {
        int fd_mgr = open(MANAGER_FILE, O_RDONLY | O_CREAT, 0644);
        if (fd_mgr < 0) return -1;
        db_lock(fd_mgr, F_RDLCK);
        char lines[MAX_RECORDS][BUFFER_SIZE];
        int count = read_all_lines(fd_mgr, lines, MAX_RECORDS);
        db_unlock(fd_mgr);
        close(fd_mgr);

        Manager target_m;
        int found = 0;
        for (int i = 0; i < count; i++) {
            Manager m;
            if (parse_manager_line(lines[i], &m) && m.id == user_id) {
                target_m = m;
                found = 1;
                break;
            }
        }
        if (!found) return 0;

        db_remove_user(user_id, ROLE_MANAGER);
        int new_eid = 0;
        db_add_employee(target_m.username, target_m.password,
                        (role_title && strlen(role_title) > 0 ? role_title : "Account Executive"),
                        &new_eid);
        return new_eid;
    }

    return -3; /* Unsupported role conversion */
}

int db_remove_user(int user_id, int role) {
    const char *filepath = NULL;
    if (role == ROLE_EMPLOYEE) filepath = EMPLOYEE_FILE;
    else if (role == ROLE_MANAGER) filepath = MANAGER_FILE;
    else if (role == ROLE_CUSTOMER) filepath = CUSTOMER_FILE;
    else return -2;

    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;

    db_lock(fd, F_WRLCK);

    char lines[MAX_RECORDS][BUFFER_SIZE];
    int count = read_all_lines(fd, lines, MAX_RECORDS);

    int found_idx = -1;
    for (int i = 0; i < count; i++) {
        int id;
        if (sscanf(lines[i], "%d |", &id) == 1 && id == user_id) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        db_unlock(fd);
        close(fd);
        return 0;
    }

    lseek(fd, 0, SEEK_SET);
    safe_truncate(fd);

    for (int i = 0; i < count; i++) {
        if (i == found_idx) continue;
        char buf[BUFFER_SIZE];
        int len = snprintf(buf, sizeof(buf), "%s\n", lines[i]);
        safe_write_all(fd, buf, len);
    }
    fsync(fd);

    db_unlock(fd);
    close(fd);
    return 1;
}
