#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h> 
#include "customer.h"
#include "loan.h"
#include "feedback.h"
#include "employee.h"
#include "manager.h"
#include "admin.h"

#ifndef TRANSACTION
#define TRANSACTION
struct transaction {
    int transactionID;
    int customerID;
    char type[50];
    float amount;
    char timestamp[30];
};
#endif

//FOR SESSION MANAGEMENT
struct active_session {
    char role[20];
    int id;
};


#define SERVER_PORT 8080
#define RESPONSE_BUFFER_SIZE 1024


void handle_client_connection(int client_socket_fd);
void handle_customer_requests(int client_socket_fd, int customer_id);
void handle_employee_requests(int client_socket_fd, int employee_id);
void handle_manager_requests(int client_socket_fd, int manager_id);
void handle_admin_requests(int client_socket_fd, int admin_id);
int validate_login(const char *role, const char *id_str, const char *password);
int is_user_logged_in(const char *role, int id);
void register_session(const char *role, int id);
void deregister_session(const char *role, int id);


int is_user_logged_in(const char *role, int id) {
    int file_desc = open("./data/sessions.data", O_RDONLY | O_CREAT, 0644);
    if (file_desc < 0) {
        perror("Could not open sessions file for check");
        return 0; // for one session per user
    }
    
    //shared lock for reading
    if (flock(file_desc, LOCK_SH) < 0) {
        perror("Could not lock sessions file for check");
        close(file_desc);
        return 0;
    }

    struct active_session session;
    int found = 0;
    while (read(file_desc, &session, sizeof(struct active_session)) == sizeof(struct active_session)) {
        if (session.id == id && strcmp(session.role, role) == 0) {
            found = 1;
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found;
}

//Adds a user to the active session file.
 
void register_session(const char *role, int id) {
    int file_desc = open("./data/sessions.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_desc < 0) {
        perror("Could not open sessions file for register");
        return;
    }

    // exclusive lock for writing
    if (flock(file_desc, LOCK_EX) < 0) {
        perror("Could not lock sessions file for register");
        close(file_desc);
        return;
    }

    struct active_session session;
    session.id = id;
    strncpy(session.role, role, sizeof(session.role) - 1);
    session.role[sizeof(session.role) - 1] = '\0';
    
    write(file_desc, &session, sizeof(struct active_session));
    
    flock(file_desc, LOCK_UN);
    close(file_desc);
    printf("Child %d: Registered session for %s %d\n", getpid(), role, id);
}

//Removes a user from the active session file (on logout/disconnect).

void deregister_session(const char *role, int id) {
    // This uses the safe rename-in-place pattern
    int read_fd = open("./data/sessions.data", O_RDONLY);
    if (read_fd < 0) {
        perror("Could not open sessions file for deregister read");
        return;
    }

    // Get an exclusive lock on the file we are reading
    if (flock(read_fd, LOCK_EX) < 0) {
        perror("Could not lock sessions file for deregister");
        close(read_fd);
        return;
    }

    int write_fd = open("./data/sessions.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (write_fd < 0) {
        perror("Could not open sessions temp file for deregister");
        flock(read_fd, LOCK_UN);
        close(read_fd);
        return;
    }

    struct active_session session;
    // Read from old file, write all *except* this user to new file
    while (read(read_fd, &session, sizeof(struct active_session)) == sizeof(struct active_session)) {
        if (session.id == id && strcmp(session.role, role) == 0) {
            // This is the user, skip writing them to the temp file
            continue;
        }
        write(write_fd, &session, sizeof(struct active_session));
    }

    flock(read_fd, LOCK_UN); // Release lock
    close(read_fd);
    close(write_fd);

    // Atomically replace the old file with the new one
    remove("./data/sessions.data");
    rename("./data/sessions.tmp", "./data/sessions.data");
    printf("Child %d: Deregistered session for %s %d\n", getpid(), role, id);
}




// Transaction Functions


int get_next_id(const char *filename, size_t record_size) {
    int file_desc = open(filename, O_RDONLY | O_CREAT, 0644);
    if (file_desc < 0) {
        perror("Error opening file for ID generation");
        return 1; // Start with 1 if file is new or unreadable
    }

    // Seek to the last record
    off_t offset = lseek(file_desc, -((long)record_size), SEEK_END);
    if (offset < 0) {
        
        close(file_desc);
        return 1; 
    }

    char buffer[record_size];
    if (read(file_desc, buffer, record_size) == record_size) {
        // Assuming the ID is the first int in the struct
        int last_id = *((int *)buffer);
        close(file_desc);
        return last_id + 1;
    }

    close(file_desc);
    return 1; 
}


void log_transaction(int customer_id, const char *type, float amount) {
    struct transaction new_txn;

    int file_desc = open("./data/transaction.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_desc < 0) {
        perror("Failed to open transaction log");
        return;
    }

    // --- Critical Section: Lock the file ---
    if (flock(file_desc, LOCK_EX) < 0) {
        perror("Failed to lock transaction log");
        close(file_desc);
        return;
    }


    new_txn.transactionID = get_next_id("./data/transaction.data", sizeof(struct transaction));
    new_txn.customerID = customer_id;
    strncpy(new_txn.type, type, sizeof(new_txn.type) - 1);
    new_txn.type[sizeof(new_txn.type) - 1] = '\0';
    new_txn.amount = amount;

    time_t now = time(NULL);
    strftime(new_txn.timestamp, sizeof(new_txn.timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    if (write(file_desc, &new_txn, sizeof(struct transaction)) != sizeof(struct transaction)) {
        perror("Failed to write transaction to log");
    }

    // Unlock the file
    flock(file_desc, LOCK_UN);
    close(file_desc);
}

// Customer Account Functions 


float get_balance(int customer_id) {
    int file_desc = open("./data/customer.data", O_RDONLY);
    if (file_desc < 0) {
        perror("File open error in get_balance");
        return -1;
    }

    struct customer current_customer;
    float balance = -1;

    if (flock(file_desc, LOCK_SH) < 0) {
         perror("Error getting shared lock on customer.data");
         close(file_desc);
         return -1;
    }

    while (read(file_desc, &current_customer, sizeof(struct customer)) == sizeof(struct customer)) {
        if (current_customer.userID == customer_id) {
            balance = current_customer.balance;
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return balance;
}


int deposit_money(int customer_id, float amount) {
    if (amount <= 0) return -1; // Invalid amount

    int file_desc = open("./data/customer.data", O_RDWR);
    if (file_desc < 0) {
        perror("File open error in deposit");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("File lock error in deposit");
        close(file_desc);
        return -1;
    }

    struct customer current_customer;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &current_customer, sizeof(struct customer)) == sizeof(struct customer)) {
        if (current_customer.userID == customer_id) {
            current_customer.balance += amount;
            current_pos = lseek(file_desc, -((long)sizeof(struct customer)), SEEK_CUR);
            
            if (current_pos == -1 || write(file_desc, &current_customer, sizeof(struct customer)) != sizeof(struct customer)) {
                perror("Failed to write updated deposit");
                found = -1; // Indicate an error
                break;
            }
            found = 1;
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);

    if (found == 1) {
        log_transaction(customer_id, "Deposit", amount);
        return 0; // Success
    }
    return -1; // Failure
}


int withdraw_money(int customer_id, float amount) {
    if (amount <= 0) return -1; // Invalid amount

    int file_desc = open("./data/customer.data", O_RDWR);
    if (file_desc < 0) {
        perror("File open error in withdraw");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("File lock error in withdraw");
        close(file_desc);
        return -1;
    }

    struct customer current_customer;
    int result = -1; // -1 = user not found, -2 = insufficient funds, 0 = success
    off_t current_pos;

    while (read(file_desc, &current_customer, sizeof(struct customer)) == sizeof(struct customer)) {
        if (current_customer.userID == customer_id) {
            if (current_customer.balance < amount) {
                result = -2; // Insufficient funds
                break;
            }
            
            current_customer.balance -= amount;
            current_pos = lseek(file_desc, -((long)sizeof(struct customer)), SEEK_CUR);
            
            if (current_pos == -1 || write(file_desc, &current_customer, sizeof(struct customer)) != sizeof(struct customer)) {
                perror("Failed to write updated withdrawal");
                break; // error
            }
            
            result = 0; // Success
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);

    if (result == 0) {
        log_transaction(customer_id, "Withdraw", amount);
    }
    return result;
}


int transfer_funds(int from_customer_id, int to_customer_id, float amount) {
    if (amount <= 0 || from_customer_id == to_customer_id) return -1;

    int file_desc = open("./data/customer.data", O_RDWR);
    if (file_desc < 0) {
        perror("File open error in transfer");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("File lock error in transfer");
        close(file_desc);
        return -1;
    }

    struct customer customer_record;
    int found_from = 0, found_to = 0;
    off_t from_pos = -1, to_pos = -1;
    float from_balance = 0;

    //Checking if both users exist & insuff. funds are not there
    lseek(file_desc, 0, SEEK_SET); 
    while (read(file_desc, &customer_record, sizeof(struct customer)) == sizeof(struct customer)) {
        if (customer_record.userID == from_customer_id) {
            if (customer_record.balance < amount) {
                flock(file_desc, LOCK_UN);
                close(file_desc);
                return -2; // Insufficient funds
            }
            from_pos = lseek(file_desc, 0, SEEK_CUR) - sizeof(struct customer);
            from_balance = customer_record.balance;
            found_from = 1;
        }
        if (customer_record.userID == to_customer_id) {
            to_pos = lseek(file_desc, 0, SEEK_CUR) - sizeof(struct customer);
            found_to = 1;
        }
    }

    if (!found_from || !found_to) {
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1; // One or both users not found
    }

    // Performing the writes
    // Update 'from' user
    lseek(file_desc, from_pos, SEEK_SET);
    read(file_desc, &customer_record, sizeof(struct customer));
    customer_record.balance -= amount;
    lseek(file_desc, from_pos, SEEK_SET);
    write(file_desc, &customer_record, sizeof(struct customer));

    // Update 'to' user
    lseek(file_desc, to_pos, SEEK_SET);
    read(file_desc, &customer_record, sizeof(struct customer));
    customer_record.balance += amount;
    lseek(file_desc, to_pos, SEEK_SET);
    write(file_desc, &customer_record, sizeof(struct customer));

    flock(file_desc, LOCK_UN);
    close(file_desc);

    // Log transactions for both users
    log_transaction(from_customer_id, "Transfer Out", amount);
    log_transaction(to_customer_id, "Transfer In", amount);
    return 0; // Success
}


int apply_for_loan(int customer_id, float amount) {
    if (amount <= 0) return -1;

    int file_desc = open("./data/loan.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_desc < 0) {
        perror("Failed to open loan data file");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("Failed to lock loan file");
        close(file_desc);
        return -1;
    }

    struct loan new_loan;
    new_loan.userID = customer_id;
    new_loan.amount = amount;
    strcpy(new_loan.status, "Pending");

    if (write(file_desc, &new_loan, sizeof(struct loan)) != sizeof(struct loan)) {
        perror("Failed to write loan application");
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1;
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return 0; // Success
}


int change_customer_password(int customer_id, const char *new_password) {
    int file_desc = open("./data/customer.data", O_RDWR);
    if (file_desc == -1) {
        perror("Error opening customer file");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("Error locking customer file");
        close(file_desc);
        return -1;
    }

    struct customer current_customer;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &current_customer, sizeof(struct customer)) == sizeof(struct customer)) {
        if (current_customer.userID == customer_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct customer)), SEEK_CUR);
            if (current_pos == -1) {
                perror("lseek failed");
                found = -1;
                break;
            }
            
            strncpy(current_customer.password, new_password, sizeof(current_customer.password) - 1);
            current_customer.password[sizeof(current_customer.password) - 1] = '\0';
            
            if (write(file_desc, &current_customer, sizeof(struct customer)) != sizeof(struct customer)) {
                perror("Failed to write updated password");
                found = -1;
            } else {
                found = 1;
            }
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return (found == 1) ? 0 : -1;
}


int add_feedback(int customer_id, const char *message) {
    int file_desc = open("./data/feedback.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_desc == -1) {
        perror("Error opening feedback file");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("Error locking feedback file");
        close(file_desc);
        return -1;
    }

    struct feedback new_feedback;
    // Getting next ID after locking
    new_feedback.feedbackID = get_next_id("./data/feedback.data", sizeof(struct feedback));
    new_feedback.customerID = customer_id;
    
    strncpy(new_feedback.message, message, sizeof(new_feedback.message) - 1);
    new_feedback.message[sizeof(new_feedback.message) - 1] = '\0';
    
    strncpy(new_feedback.status, "Pending", sizeof(new_feedback.status) - 1);
    new_feedback.status[sizeof(new_feedback.status) - 1] = '\0';

    if (write(file_desc, &new_feedback, sizeof(struct feedback)) != sizeof(struct feedback)) {
        perror("Failed to write feedback entry");
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1;
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return 0; // Success
}


int view_transaction_history(int client_socket_fd, int customer_id) {
    int file_desc = open("./data/transaction.data", O_RDONLY);
    if (file_desc == -1) {
        perror("Error opening transactions file");
        return -1;
    }

    if (flock(file_desc, LOCK_SH) < 0) {
        perror("Error locking transaction file");
        close(file_desc);
        return -1;
    }

    struct transaction txn;
    int transaction_found = 0;
    char buffer[512];

    while (read(file_desc, &txn, sizeof(struct transaction)) == sizeof(struct transaction)) {
        if (txn.customerID == customer_id) {
            snprintf(buffer, sizeof(buffer), "ID: %d | Type: %s | Amount: %.2f | Date: %s\n",
                     txn.transactionID, txn.type, txn.amount, txn.timestamp);
            
            if (write(client_socket_fd, buffer, strlen(buffer)) < 0) {
                perror("Failed to send transaction data");
                transaction_found = -1; // Indicate error
                break;
            }
            transaction_found = 1;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);

    if (transaction_found == 0) {
        char no_history_msg[] = "No transaction history found.\n";
        write(client_socket_fd, no_history_msg, strlen(no_history_msg));
    }

    //after this the client stops listening
    write(client_socket_fd, "END_OF_RESPONSE", strlen("END_OF_RESPONSE"));
    return 0;
}


void handle_customer_requests(int client_socket_fd, int customer_id) {
    char command[RESPONSE_BUFFER_SIZE];
    char response[RESPONSE_BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = read(client_socket_fd, command, sizeof(command) - 1)) > 0) {
        command[bytes_read] = '\0';

        if (strcmp(command, "VIEW_BALANCE") == 0) {
            float balance = get_balance(customer_id);
            if (balance >= 0) {
                snprintf(response, sizeof(response), "%.2f", balance);
            } else {
                snprintf(response, sizeof(response), "Error: Could not retrieve balance.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "DEPOSIT_MONEY") == 0) {
            char amount_str[50];
            bytes_read = read(client_socket_fd, amount_str, sizeof(amount_str) - 1);
            amount_str[bytes_read] = '\0';
            float amount = atof(amount_str);

            if (deposit_money(customer_id, amount) == 0) {
                snprintf(response, sizeof(response), "Deposit of %.2f successful.", amount);
            } else {
                snprintf(response, sizeof(response), "Deposit failed. Invalid amount?");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "WITHDRAW_MONEY") == 0) {
            char amount_str[50];
            bytes_read = read(client_socket_fd, amount_str, sizeof(amount_str) - 1);
            amount_str[bytes_read] = '\0';
            float amount = atof(amount_str);
            
            int status = withdraw_money(customer_id, amount);
            if (status == 0) {
                snprintf(response, sizeof(response), "Withdrawal of %.2f successful.", amount);
            } else if (status == -2) {
                snprintf(response, sizeof(response), "Withdrawal failed. Insufficient funds.");
            } else {
                snprintf(response, sizeof(response), "Withdrawal failed. Invalid amount or error.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "TRANSFER_FUNDS") == 0) {
            char to_user_str[50], amount_str[50];
            bytes_read = read(client_socket_fd, to_user_str, sizeof(to_user_str) - 1);
            to_user_str[bytes_read] = '\0';
            int to_user_id = atoi(to_user_str);

            bytes_read = read(client_socket_fd, amount_str, sizeof(amount_str) - 1);
            amount_str[bytes_read] = '\0';
            float amount = atof(amount_str);

            int status = transfer_funds(customer_id, to_user_id, amount);
            if (status == 0) {
                snprintf(response, sizeof(response), "Transfer of %.2f to user %d successful.", amount, to_user_id);
            } else if (status == -2) {
                snprintf(response, sizeof(response), "Transfer failed. Insufficient funds.");
            } else {
                snprintf(response, sizeof(response), "Transfer failed. Invalid user or amount.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "APPLY_LOAN") == 0) {
            char amount_str[50];
            bytes_read = read(client_socket_fd, amount_str, sizeof(amount_str) - 1);
            amount_str[bytes_read] = '\0';
            float amount = atof(amount_str);

            if (apply_for_loan(customer_id, amount) == 0) {
                snprintf(response, sizeof(response), "Loan application for %.2f submitted.", amount);
            } else {
                snprintf(response, sizeof(response), "Loan application failed.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "CHANGE_PWD") == 0) {
            char new_pass[50];
            bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1);
            new_pass[bytes_read] = '\0';
            // No need to strip newline, client already does it
            
            if (change_customer_password(customer_id, new_pass) == 0) {
                snprintf(response, sizeof(response), "Password changed successfully.");
            } else {
                snprintf(response, sizeof(response), "Password change failed.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "FEEDBACK") == 0) {
            char feedback_text[256]; // Matches feedback.h
            bytes_read = read(client_socket_fd, feedback_text, sizeof(feedback_text) - 1);
            feedback_text[bytes_read] = '\0';
            // No need to strip newline, client already does it

            int status = add_feedback(customer_id, feedback_text);
            
            if (status == 0) {
                snprintf(response, sizeof(response), "Feedback successfully submitted.");
            } else {
                snprintf(response, sizeof(response), "Failed to submit feedback.");
            }
            
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "VIEW_TRANSACTION") == 0) {
            view_transaction_history(client_socket_fd, customer_id);
            // No other response needed, view_transaction_history handles it all
        
        } else if (strcmp(command, "LOGOUT") == 0) {
            snprintf(response, sizeof(response), "Logging out customer %d...", customer_id);
            write(client_socket_fd, response, strlen(response));
            return; // Exit the loop and function
        }
    }
}

// Employee Functions


int add_customer(int user_id, const char *fname, const char *lname, const char *pwd, float balance) {
    int file_desc = open("./data/customer.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_desc < 0) {
        perror("Failed to open customer file");
        return -1;
    }

    if (flock(file_desc, LOCK_EX) < 0) {
        perror("Failed to lock customer file");
        close(file_desc);
        return -1;
    }

    // Checking if that user ID already exists
    struct customer temp_cust;
    int id_exists = 0;
    
    int read_fd = open("./data/customer.data", O_RDONLY);
    if (read_fd >= 0) {
        while (read(read_fd, &temp_cust, sizeof(struct customer)) == sizeof(struct customer)) {
            if (temp_cust.userID == user_id) {
                id_exists = 1;
                break;
            }
        }
        close(read_fd);
    }
    
    if (id_exists) {
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -2; // ID already exists so cannot add this user
    }

    struct customer new_customer;
    new_customer.userID = user_id;
    strncpy(new_customer.firstName, fname, sizeof(new_customer.firstName) - 1);
    new_customer.firstName[sizeof(new_customer.firstName) - 1] = '\0';
    strncpy(new_customer.lastName, lname, sizeof(new_customer.lastName) - 1);
    new_customer.lastName[sizeof(new_customer.lastName) - 1] = '\0';
    strncpy(new_customer.password, pwd, sizeof(new_customer.password) - 1);
    new_customer.password[sizeof(new_customer.password) - 1] = '\0';
    new_customer.balance = balance;
    new_customer.loan = 0;
    strncpy(new_customer.status, "Active", sizeof(new_customer.status) - 1);
    new_customer.status[sizeof(new_customer.status) - 1] = '\0';

    if (write(file_desc, &new_customer, sizeof(struct customer)) != sizeof(struct customer)) {
        perror("Failed to add new customer");
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1;
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return 0; // Success
}


int get_customer_details(int customer_id, struct customer *cust_out) {
    int file_desc = open("./data/customer.data", O_RDONLY);
    if (file_desc < 0) return -1;
    
    if (flock(file_desc, LOCK_SH) < 0) {
        close(file_desc);
        return -1;
    }

    int found = 0;
    while (read(file_desc, cust_out, sizeof(struct customer)) == sizeof(struct customer)) {
        if (cust_out->userID == customer_id) {
            found = 1;
            break;
        }
    }
    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


int update_customer_details(int customer_id, struct customer *cust_in) {
    int file_desc = open("./data/customer.data", O_RDWR);
    if (file_desc < 0) return -1;

    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    int found = 0;
    struct customer temp_cust;
    off_t current_pos;
    
    while (read(file_desc, &temp_cust, sizeof(struct customer)) == sizeof(struct customer)) {
        if (temp_cust.userID == customer_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct customer)), SEEK_CUR);
            lseek(file_desc, current_pos, SEEK_SET);
            
            if (write(file_desc, cust_in, sizeof(struct customer)) == sizeof(struct customer)) {
                found = 1;
            }
            break;
        }
    }
    
    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


int delete_customer(int customer_id) {

    // A simple lock on the original file is needed.
    int file_desc = open("./data/customer.data", O_RDWR); // Open RDWR for EX lock
    if (file_desc < 0) return -1;
    
    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    int temp_fd = open("./data/customer.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1;
    }

    // Rewind the locked file
    lseek(file_desc, 0, SEEK_SET);
    
    struct customer temp_cust;
    int found = 0;
    while (read(file_desc, &temp_cust, sizeof(struct customer)) == sizeof(struct customer)) {
        if (temp_cust.userID == customer_id) {
            found = 1;
        } else {
            write(temp_fd, &temp_cust, sizeof(struct customer));
        }
    }
    
    // Close handles before renaming
    close(file_desc); // locks are released too
    close(temp_fd);

    if (found) {
        remove("./data/customer.data");
        rename("./data/customer.tmp", "./data/customer.data");
    } else {
        remove("./data/customer.tmp"); // No changes, remove temp file
    }
    
    return found ? 0 : -1;
}



int get_assigned_loans(int employee_id, char *details_buffer, size_t buffer_size) {
    int emp_fd = open("./data/employee.data", O_RDONLY);
    if (emp_fd < 0) return -1;

    if (flock(emp_fd, LOCK_SH) < 0) {
        close(emp_fd);
        return -1;
    }

    struct employee emp;
    int found = 0;
    int assigned_loans[10];
    int loan_count = 0;

    while (read(emp_fd, &emp, sizeof(struct employee)) == sizeof(struct employee)) {
        if (emp.employeeID == employee_id) {
            found = 1;
            loan_count = emp.loan_count;
            memcpy(assigned_loans, emp.assigned_loans, sizeof(emp.assigned_loans));
            break;
        }
    }
    flock(emp_fd, LOCK_UN);
    close(emp_fd);

    if (!found || loan_count == 0) {
        snprintf(details_buffer, buffer_size, "No loan applications assigned.\n");
        return 0;
    }

    int loan_fd = open("./data/loan.data", O_RDONLY);
    if (loan_fd < 0) {
        snprintf(details_buffer, buffer_size, "Error: Could not open loan file.\n");
        return -1;
    }
    if (flock(loan_fd, LOCK_SH) < 0) {
        close(loan_fd);
        return -1;
    }

    details_buffer[0] = '\0'; // Start with an empty string
    struct loan temp_loan;
    int loans_found = 0;

    while(read(loan_fd, &temp_loan, sizeof(struct loan)) == sizeof(struct loan)) {
        for (int i = 0; i < loan_count; i++) {
            if (temp_loan.userID == assigned_loans[i]) {
                char loan_entry[200];
                snprintf(loan_entry, sizeof(loan_entry), "Loan for User ID: %d | Amount: %.2f | Status: %s\n",
                         temp_loan.userID, temp_loan.amount, temp_loan.status);
                
                if (strlen(details_buffer) + strlen(loan_entry) < buffer_size) {
                    strcat(details_buffer, loan_entry);
                    loans_found++;
                } else {
                    break; // Buffer full
                }
            }
        }
        if (loans_found == loan_count) break; // Found all
    }
    flock(loan_fd, LOCK_UN);
    close(loan_fd);

    if (loans_found == 0) {
        snprintf(details_buffer, buffer_size, "No details found for assigned loans (may be processed).\n");
    }
    
    return 0;
}


int update_loan_status(int loan_user_id, const char *new_status) {
    int loan_fd = open("./data/loan.data", O_RDWR);
    if (loan_fd < 0) return -1;

    if (flock(loan_fd, LOCK_EX) < 0) {
        close(loan_fd);
        return -1;
    }

    struct loan temp_loan;
    int found = 0;
    float amount = 0;
    off_t current_pos;

    while (read(loan_fd, &temp_loan, sizeof(struct loan)) == sizeof(struct loan)) {
        if (temp_loan.userID == loan_user_id && strcmp(temp_loan.status, "Pending") == 0) {
            current_pos = lseek(loan_fd, -((long)sizeof(struct loan)), SEEK_CUR);
            
            strncpy(temp_loan.status, new_status, sizeof(temp_loan.status) - 1);
            temp_loan.status[sizeof(temp_loan.status) - 1] = '\0';
            amount = temp_loan.amount;

            lseek(loan_fd, current_pos, SEEK_SET);
            if (write(loan_fd, &temp_loan, sizeof(struct loan)) == sizeof(struct loan)) {
                found = 1;
            }
            break;
        }
    }
    flock(loan_fd, LOCK_UN);
    close(loan_fd);

    if (found && strcmp(new_status, "Approved") == 0) {
        // Credit the customer's account
        int cust_fd = open("./data/customer.data", O_RDWR);
        if (cust_fd < 0) return -1;

        if (flock(cust_fd, LOCK_EX) < 0) {
            close(cust_fd);
            return -1;
        }

        struct customer temp_cust;
        while (read(cust_fd, &temp_cust, sizeof(struct customer)) == sizeof(struct customer)) {
            if (temp_cust.userID == loan_user_id) {
                temp_cust.balance += amount;
                temp_cust.loan += amount; // Track total loan amount
                
                current_pos = lseek(cust_fd, -((long)sizeof(struct customer)), SEEK_CUR);
                lseek(cust_fd, current_pos, SEEK_SET);
                write(cust_fd, &temp_cust, sizeof(struct customer));
                
                log_transaction(loan_user_id, "Loan Approved", amount);
                break;
            }
        }
        flock(cust_fd, LOCK_UN);
        close(cust_fd);
    }
    
    return found ? 0 : -1;
}


int change_employee_password(int employee_id, const char *new_password) {
    int file_desc = open("./data/employee.data", O_RDWR);
    if (file_desc == -1) return -1;

    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    struct employee temp_emp;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
        if (temp_emp.employeeID == employee_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct employee)), SEEK_CUR);
            
            strncpy(temp_emp.password, new_password, sizeof(temp_emp.password) - 1);
            temp_emp.password[sizeof(temp_emp.password) - 1] = '\0';
            
            lseek(file_desc, current_pos, SEEK_SET);
            if (write(file_desc, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
                found = 1;
            }
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


void handle_employee_requests(int client_socket_fd, int employee_id) {
    char command[RESPONSE_BUFFER_SIZE];
    char response[RESPONSE_BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = read(client_socket_fd, command, sizeof(command) - 1)) > 0) {
        command[bytes_read] = '\0';
        response[0] = '\0'; // Clear response buffer

        if (strcmp(command, "ADD_CUSTOMER") == 0) {
            char userid_str[20], fname[50], lname[50], pwd[50], balance_str[20];
            
            bytes_read = read(client_socket_fd, userid_str, sizeof(userid_str) - 1); userid_str[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, fname, sizeof(fname) - 1); fname[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, lname, sizeof(lname) - 1); lname[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, pwd, sizeof(pwd) - 1); pwd[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, balance_str, sizeof(balance_str) - 1); balance_str[bytes_read] = '\0';
            
            // Clean strings
            userid_str[strcspn(userid_str, "\n")] = 0;
            fname[strcspn(fname, "\n")] = 0;
            lname[strcspn(lname, "\n")] = 0;
            pwd[strcspn(pwd, "\n")] = 0;
            balance_str[strcspn(balance_str, "\n")] = 0;

            int status = add_customer(atoi(userid_str), fname, lname, pwd, atof(balance_str));
            if (status == 0) {
                snprintf(response, sizeof(response), "Customer added successfully.");
            } else if (status == -2) {
                snprintf(response, sizeof(response), "Error: Customer ID %s already exists.", userid_str);
            } else {
                snprintf(response, sizeof(response), "Error: Failed to add customer.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "MANAGE_CUSTOMER") == 0) {
            char id_str[20];
            bytes_read = read(client_socket_fd, id_str, sizeof(id_str) - 1); id_str[bytes_read] = '\0';
            id_str[strcspn(id_str, "\n")] = 0;
            int cust_id = atoi(id_str);

            struct customer cust;
            if (get_customer_details(cust_id, &cust) != 0) {
                snprintf(response, sizeof(response), "Error: Customer with ID %d not found.", cust_id);
                write(client_socket_fd, response, strlen(response));
                
                // Read and discard the sub-choice from client to prevent de-sync
                read(client_socket_fd, id_str, sizeof(id_str) -1);
                snprintf(response, sizeof(response), "Error: Action cancelled.");
                write(client_socket_fd, response, strlen(response));
                continue;
            }

            // Send current details
            snprintf(response, sizeof(response), "ID: %d | Name: %s %s | Status: %s | Balance: %.2f | Loan: %.2f",
                     cust.userID, cust.firstName, cust.lastName, cust.status, cust.balance, cust.loan);
            write(client_socket_fd, response, strlen(response));

            // Read sub-choice
            char choice_str[10];
            bytes_read = read(client_socket_fd, choice_str, sizeof(choice_str) - 1);
            choice_str[bytes_read] = '\0';
            int sub_choice = atoi(choice_str);

            if (sub_choice == 1) { // Modify
                char fname[50], lname[50], balance_str[20], loan_str[20], status[20];
                bytes_read = read(client_socket_fd, fname, sizeof(fname) - 1); fname[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, lname, sizeof(lname) - 1); lname[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, balance_str, sizeof(balance_str) - 1); balance_str[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, loan_str, sizeof(loan_str) - 1); loan_str[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, status, sizeof(status) - 1); status[bytes_read] = '\0';

                // Update struct
                strncpy(cust.firstName, fname, sizeof(cust.firstName) - 1);
                cust.firstName[strcspn(cust.firstName, "\n")] = 0;
                strncpy(cust.lastName, lname, sizeof(cust.lastName) - 1);
                cust.lastName[strcspn(cust.lastName, "\n")] = 0;
                cust.balance = atof(balance_str);
                cust.loan = atof(loan_str);
                strncpy(cust.status, status, sizeof(cust.status) - 1);
                cust.status[strcspn(cust.status, "\n")] = 0;

                if (update_customer_details(cust_id, &cust) == 0) {
                    snprintf(response, sizeof(response), "Customer details updated.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to update details.");
                }
            } else if (sub_choice == 2) { // Delete
                if (delete_customer(cust_id) == 0) {
                    snprintf(response, sizeof(response), "Customer deleted successfully.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to delete customer.");
                }
            } else if (sub_choice == 3) { // Reset Password
                char new_pass[50];
                bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1); new_pass[bytes_read] = '\0';
                new_pass[strcspn(new_pass, "\n")] = 0;

                if (change_customer_password(cust_id, new_pass) == 0) {
                    snprintf(response, sizeof(response), "Customer password reset.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to reset password.");
                }
            } else { // Cancel
                snprintf(response, sizeof(response), "Customer management cancelled.");
            }
            write(client_socket_fd, response, strlen(response));

        } else if (strcmp(command, "VIEW_LOAN_APPL") == 0) {
            char details_buffer[RESPONSE_BUFFER_SIZE * 2]; // Larger buffer for list
            get_assigned_loans(employee_id, details_buffer, sizeof(details_buffer));
            write(client_socket_fd, details_buffer, strlen(details_buffer));
            write(client_socket_fd, "END_OF_RESPONSE", strlen("END_OF_RESPONSE")); // Send end marker
        
        } else if (strcmp(command, "APP/REJ_LOANS") == 0) {
            char loan_id_str[20], new_status[20];
            bytes_read = read(client_socket_fd, loan_id_str, sizeof(loan_id_str) - 1); loan_id_str[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, new_status, sizeof(new_status) - 1); new_status[bytes_read] = '\0';
            loan_id_str[strcspn(loan_id_str, "\n")] = 0;
            new_status[strcspn(new_status, "\n")] = 0;

            if (update_loan_status(atoi(loan_id_str), new_status) == 0) {
                snprintf(response, sizeof(response), "Loan for user %s status set to %s.", loan_id_str, new_status);
            } else {
                snprintf(response, sizeof(response), "Error: Failed to update loan status. Already processed?");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "VIEW_CUST_TRANS") == 0) {
            char id_str[20];
            bytes_read = read(client_socket_fd, id_str, sizeof(id_str) - 1); id_str[bytes_read] = '\0';
            id_str[strcspn(id_str, "\n")] = 0;
            view_transaction_history(client_socket_fd, atoi(id_str));
            // No other response needed
        
        } else if (strcmp(command, "CHANGE_PWD") == 0) {
            char new_pass[50];
            bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1); new_pass[bytes_read] = '\0';
            new_pass[strcspn(new_pass, "\n")] = 0;
            
            if (change_employee_password(employee_id, new_pass) == 0) {
                snprintf(response, sizeof(response), "Password changed successfully.");
            } else {
                snprintf(response, sizeof(response), "Password change failed.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "LOGOUT") == 0) {
            snprintf(response, sizeof(response), "Logging out employee %d...", employee_id);
            write(client_socket_fd, response, strlen(response));
            return; // Exit loop
        }
    }
}

// Manager Functions


int set_customer_status(int customer_id, const char *action) {
    char new_status[20];
    if (strcmp(action, "ACTIVATE") == 0) {
        strcpy(new_status, "Active");
    } else if (strcmp(action, "DEACTIVATE") == 0) {
        strcpy(new_status, "Inactive");
    } else {
        return -1; // Invalid action
    }

    int file_desc = open("./data/customer.data", O_RDWR);
    if (file_desc < 0) return -1;
    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    struct customer temp_cust;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &temp_cust, sizeof(struct customer)) == sizeof(struct customer)) {
        if (temp_cust.userID == customer_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct customer)), SEEK_CUR);
            
            strncpy(temp_cust.status, new_status, sizeof(temp_cust.status) - 1);
            temp_cust.status[sizeof(temp_cust.status) - 1] = '\0';
            
            lseek(file_desc, current_pos, SEEK_SET);
            if (write(file_desc, &temp_cust, sizeof(struct customer)) == sizeof(struct customer)) {
                found = 1;
            }
            break;
        }
    }
    
    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


int assign_loan_to_employee(int employee_id, int loan_user_id) {
    // Check if loan is valid and "Pending"
    int loan_fd = open("./data/loan.data", O_RDONLY);
    if (loan_fd < 0) return -1;
    if (flock(loan_fd, LOCK_SH) < 0) { close(loan_fd); return -1; }
    
    struct loan temp_loan;
    int loan_valid = 0;
    while(read(loan_fd, &temp_loan, sizeof(struct loan)) == sizeof(struct loan)) {
        if (temp_loan.userID == loan_user_id && strcmp(temp_loan.status, "Pending") == 0) {
            loan_valid = 1;
            break;
        }
    }
    flock(loan_fd, LOCK_UN);
    close(loan_fd);
    if (!loan_valid) return -4; // Loan not found or not pending

    // Assign to employee
    int file_desc = open("./data/employee.data", O_RDWR);
    if (file_desc < 0) return -1;

    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    struct employee temp_emp;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
        if (temp_emp.employeeID == employee_id) {
            if (temp_emp.loan_count >= 10) {
                found = -2; // Employee loan queue is full
                break;
            }
            
            // Check if loan already assigned
            for(int i=0; i < temp_emp.loan_count; i++) {
                if (temp_emp.assigned_loans[i] == loan_user_id) {
                    found = -3; // Loan already assigned
                    break;
                }
            }
            if (found == -3) break;

            current_pos = lseek(file_desc, -((long)sizeof(struct employee)), SEEK_CUR);
            
            temp_emp.assigned_loans[temp_emp.loan_count] = loan_user_id;
            temp_emp.loan_count++;
            
            lseek(file_desc, current_pos, SEEK_SET);
            if (write(file_desc, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
                found = 1; // Success
            }
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    
    if (found == 1) return 0;
    if (found == -2) return -2;
    if (found == -3) return -3;
    if (found == -4) return -4;
    return -1; // Employee not found
}


int get_all_feedback(char *details_buffer, size_t buffer_size) {
    int file_desc = open("./data/feedback.data", O_RDONLY);
    if (file_desc < 0) {
        snprintf(details_buffer, buffer_size, "Error: Could not open feedback file.\n");
        return -1;
    }
    
    if (flock(file_desc, LOCK_SH) < 0) {
        close(file_desc);
        snprintf(details_buffer, buffer_size, "Error: Could not lock feedback file.\n");
        return -1;
    }

    details_buffer[0] = '\0';
    struct feedback temp_fb;
    int feedback_found = 0;

    while(read(file_desc, &temp_fb, sizeof(struct feedback)) == sizeof(struct feedback)) {
        // Show all feedback, not just pending
        char fb_entry[400];
        snprintf(fb_entry, sizeof(fb_entry), "ID: %d | Cust ID: %d | Status: %s\nMsg: %s\n\n",
                    temp_fb.feedbackID, temp_fb.customerID, temp_fb.status, temp_fb.message);
        
        if (strlen(details_buffer) + strlen(fb_entry) < buffer_size) {
            strcat(details_buffer, fb_entry);
            feedback_found++;
        }
    }
    flock(file_desc, LOCK_UN);
    close(file_desc);

    if (feedback_found == 0) {
        snprintf(details_buffer, buffer_size, "No feedback available.\n");
    }

    return 0;
}


int review_feedback(int feedback_id) {
    if (feedback_id == 0) return 0; // User chose to skip

    int file_desc = open("./data/feedback.data", O_RDWR);
    if (file_desc < 0) return -1;
    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    struct feedback temp_fb;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &temp_fb, sizeof(struct feedback)) == sizeof(struct feedback)) {
        if (temp_fb.feedbackID == feedback_id && strcmp(temp_fb.status, "Pending") == 0) {
            current_pos = lseek(file_desc, -((long)sizeof(struct feedback)), SEEK_CUR);
            
            strncpy(temp_fb.status, "Reviewed", sizeof(temp_fb.status) - 1);
            temp_fb.status[sizeof(temp_fb.status) - 1] = '\0';
            
            lseek(file_desc, current_pos, SEEK_SET);
            if (write(file_desc, &temp_fb, sizeof(struct feedback)) == sizeof(struct feedback)) {
                found = 1;
            }
            break;
        }
    }
    
    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


int change_manager_password(int manager_id, const char *new_password) {
    int file_desc = open("./data/manager.data", O_RDWR);
    if (file_desc == -1) return -1;

    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    struct manager temp_mgr;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &temp_mgr, sizeof(struct manager)) == sizeof(struct manager)) {
        if (temp_mgr.managerID == manager_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct manager)), SEEK_CUR);
            
            strncpy(temp_mgr.password, new_password, sizeof(temp_mgr.password) - 1);
            temp_mgr.password[sizeof(temp_mgr.password) - 1] = '\0';
            
            lseek(file_desc, current_pos, SEEK_SET);
            if (write(file_desc, &temp_mgr, sizeof(struct manager)) == sizeof(struct manager)) {
                found = 1;
            }
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


void handle_manager_requests(int client_socket_fd, int manager_id) {
    char command[RESPONSE_BUFFER_SIZE];
    char response[RESPONSE_BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = read(client_socket_fd, command, sizeof(command) - 1)) > 0) {
        command[bytes_read] = '\0';
        response[0] = '\0';

        if (strcmp(command, "ACT/DEACT_CUST") == 0) {
            char cust_id_str[20], action[20];
            bytes_read = read(client_socket_fd, cust_id_str, sizeof(cust_id_str) - 1); cust_id_str[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, action, sizeof(action) - 1); action[bytes_read] = '\0';
            cust_id_str[strcspn(cust_id_str, "\n")] = 0;
            action[strcspn(action, "\n")] = 0;

            if (set_customer_status(atoi(cust_id_str), action) == 0) {
                snprintf(response, sizeof(response), "Customer %s status set to %s.", cust_id_str, action);
            } else {
                snprintf(response, sizeof(response), "Error: Failed to update customer status.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "ASSIGN_LOAN") == 0) {
            char loan_id_str[20], emp_id_str[20];
            bytes_read = read(client_socket_fd, loan_id_str, sizeof(loan_id_str) - 1); loan_id_str[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, emp_id_str, sizeof(emp_id_str) - 1); emp_id_str[bytes_read] = '\0';
            loan_id_str[strcspn(loan_id_str, "\n")] = 0;
            emp_id_str[strcspn(emp_id_str, "\n")] = 0;
            
            int status = assign_loan_to_employee(atoi(emp_id_str), atoi(loan_id_str));
            if (status == 0) {
                snprintf(response, sizeof(response), "Loan %s assigned to employee %s.", loan_id_str, emp_id_str);
            } else if (status == -2) {
                snprintf(response, sizeof(response), "Error: Employee's loan queue is full.");
            } else if (status == -3) {
                snprintf(response, sizeof(response), "Error: Loan is already assigned to this employee.");
            } else if (status == -4) {
                snprintf(response, sizeof(response), "Error: Loan not found or is not 'Pending'.");
            } else {
                snprintf(response, sizeof(response), "Error: Failed to assign loan. Invalid employee ID?");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "VIEW_FEEDBACK") == 0) {
            char details_buffer[RESPONSE_BUFFER_SIZE * 4]; // Large buffer
            get_all_feedback(details_buffer, sizeof(details_buffer));
            write(client_socket_fd, details_buffer, strlen(details_buffer));
            write(client_socket_fd, "END_OF_RESPONSE", strlen("END_OF_RESPONSE"));

            // Wait for manager to send ID to review
            char fb_id_str[20];
            bytes_read = read(client_socket_fd, fb_id_str, sizeof(fb_id_str) - 1); fb_id_str[bytes_read] = '\0';
            fb_id_str[strcspn(fb_id_str, "\n")] = 0;
            int fb_id = atoi(fb_id_str);
            
            if (fb_id == 0) {
                snprintf(response, sizeof(response), "No feedback reviewed.");
            } else if (review_feedback(fb_id) == 0) {
                snprintf(response, sizeof(response), "Feedback ID %d marked as 'Reviewed'.", fb_id);
            } else {
                snprintf(response, sizeof(response), "Error: Could not review feedback ID %d (not pending?).", fb_id);
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "CHANGE_PWD") == 0) {
            char new_pass[50];
            bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1); new_pass[bytes_read] = '\0';
            new_pass[strcspn(new_pass, "\n")] = 0;
            
            if (change_manager_password(manager_id, new_pass) == 0) {
                snprintf(response, sizeof(response), "Password changed successfully.");
            } else {
                snprintf(response, sizeof(response), "Password change failed.");
            }
            write(client_socket_fd, response, strlen(response));

        } else if (strcmp(command, "LOGOUT") == 0) {
            snprintf(response, sizeof(response), "Logging out manager %d...", manager_id);
            write(client_socket_fd, response, strlen(response));
            return; // Exit loop
        }
    }
}

// Admin Functions


int add_employee(int employee_id, const char *fname, const char *lname, const char *pwd) {
    int file_desc = open("./data/employee.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_desc < 0) return -1;
    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    // Checking for existing ID
    struct employee temp_emp;
    int id_exists = 0;
    int read_fd = open("./data/employee.data", O_RDONLY);
    if (read_fd >= 0) {
        while (read(read_fd, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
            if (temp_emp.employeeID == employee_id) {
                id_exists = 1;
                break;
            }
        }
        close(read_fd);
    }
    
    if (id_exists) {
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -2; // ID already exists
    }

    struct employee new_employee;
    new_employee.employeeID = employee_id;
    strncpy(new_employee.first_name, fname, sizeof(new_employee.first_name) - 1);
    new_employee.first_name[sizeof(new_employee.first_name) - 1] = '\0';
    strncpy(new_employee.last_name, lname, sizeof(new_employee.last_name) - 1);
    new_employee.last_name[sizeof(new_employee.last_name) - 1] = '\0';
    strncpy(new_employee.password, pwd, sizeof(new_employee.password) - 1);
    new_employee.password[sizeof(new_employee.password) - 1] = '\0';
    new_employee.loan_count = 0;
    memset(new_employee.assigned_loans, 0, sizeof(new_employee.assigned_loans));
    strncpy(new_employee.status, "Active", sizeof(new_employee.status) - 1);
    new_employee.status[sizeof(new_employee.status) - 1] = '\0';

    if (write(file_desc, &new_employee, sizeof(struct employee)) != sizeof(struct employee)) {
        perror("Failed to write new employee");
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1;
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return 0; // Success
}


int get_employee_details(int employee_id, struct employee *emp_out) {
    int file_desc = open("./data/employee.data", O_RDONLY);
    if (file_desc < 0) return -1;
    
    if (flock(file_desc, LOCK_SH) < 0) {
        close(file_desc);
        return -1;
    }

    int found = 0;
    while (read(file_desc, emp_out, sizeof(struct employee)) == sizeof(struct employee)) {
        if (emp_out->employeeID == employee_id) {
            found = 1;
            break;
        }
    }
    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


int update_employee_details(int employee_id, struct employee *emp_in) {
    int file_desc = open("./data/employee.data", O_RDWR);
    if (file_desc < 0) return -1;

    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    int found = 0;
    struct employee temp_emp;
    off_t current_pos;
    
    while (read(file_desc, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
        if (temp_emp.employeeID == employee_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct employee)), SEEK_CUR);
            lseek(file_desc, current_pos, SEEK_SET);
            
            // Critical: Don't overwrite assigned loans info
            emp_in->loan_count = temp_emp.loan_count;
            memcpy(emp_in->assigned_loans, temp_emp.assigned_loans, sizeof(emp_in->assigned_loans));
            
            if (write(file_desc, emp_in, sizeof(struct employee)) == sizeof(struct employee)) {
                found = 1;
            }
            break;
        }
    }
    
    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


int delete_employee(int employee_id) {
    int file_desc = open("./data/employee.data", O_RDWR);
    if (file_desc < 0) return -1;
    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }
    
    int temp_fd = open("./data/employee.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        flock(file_desc, LOCK_UN);
        close(file_desc);
        return -1;
    }

    lseek(file_desc, 0, SEEK_SET);

    struct employee temp_emp;
    int found = 0;
    while (read(file_desc, &temp_emp, sizeof(struct employee)) == sizeof(struct employee)) {
        if (temp_emp.employeeID == employee_id) {
            found = 1;
        } else {
            write(temp_fd, &temp_emp, sizeof(struct employee));
        }
    }
    
    close(file_desc); // releases lock
    close(temp_fd);

    if (found) {
        remove("./data/employee.data");
        rename("./data/employee.tmp", "./data/employee.data");
    } else {
        remove("./data/employee.tmp");
    }
    
    return found ? 0 : -1;
}


int promote_to_manager(int employee_id) {
    struct employee emp;
    if (get_employee_details(employee_id, &emp) != 0) {
        return -1; // Employee not found
    }
    
    // Check if manager ID already exists
    int mgr_read_fd = open("./data/manager.data", O_RDONLY | O_CREAT, 0644);
    if(mgr_read_fd < 0) return -1;
    if(flock(mgr_read_fd, LOCK_SH) < 0) { close(mgr_read_fd); return -1; }
    
    struct manager temp_mgr;
    int id_exists = 0;
    while(read(mgr_read_fd, &temp_mgr, sizeof(struct manager)) == sizeof(struct manager)) {
        if(temp_mgr.managerID == employee_id) {
            id_exists = 1;
            break;
        }
    }
    flock(mgr_read_fd, LOCK_UN);
    close(mgr_read_fd);
    if(id_exists) return -2; // Manager ID already exists

    // Add to manager file
    int mgr_fd = open("./data/manager.data", O_WRONLY | O_APPEND);
    if (mgr_fd < 0) return -1;
    if (flock(mgr_fd, LOCK_EX) < 0) {
        close(mgr_fd);
        return -1;
    }

    struct manager new_mgr;
    new_mgr.managerID = emp.employeeID;
    strncpy(new_mgr.first_name, emp.first_name, sizeof(new_mgr.first_name) - 1);
    new_mgr.first_name[sizeof(new_mgr.first_name) - 1] = '\0';
    strncpy(new_mgr.last_name, emp.last_name, sizeof(new_mgr.last_name) - 1);
    new_mgr.last_name[sizeof(new_mgr.last_name) - 1] = '\0';
    strncpy(new_mgr.password, emp.password, sizeof(new_mgr.password) - 1);
    new_mgr.password[sizeof(new_mgr.password) - 1] = '\0';
    
    write(mgr_fd, &new_mgr, sizeof(struct manager));
    
    flock(mgr_fd, LOCK_UN);
    close(mgr_fd);

    // Delete from employee file
    delete_employee(employee_id);
    return 0;
}


int demote_to_employee(int manager_id) {
    // Get manager details
    int mgr_fd = open("./data/manager.data", O_RDWR); // Open RDWR for lock
    if (mgr_fd < 0) return -1;
    if (flock(mgr_fd, LOCK_EX) < 0) { close(mgr_fd); return -1; }
    
    struct manager temp_mgr;
    int found = 0;
    while(read(mgr_fd, &temp_mgr, sizeof(struct manager)) == sizeof(struct manager)) {
        if (temp_mgr.managerID == manager_id) {
            found = 1;
            break;
        }
    }
    
    if (!found) {
        flock(mgr_fd, LOCK_UN);
        close(mgr_fd);
        return -1; // Manager not found
    }

    // Add to employee file
    int add_status = add_employee(temp_mgr.managerID, temp_mgr.first_name, temp_mgr.last_name, temp_mgr.password);
    if (add_status == -2) { // Employee ID already exists
        flock(mgr_fd, LOCK_UN);
        close(mgr_fd);
        return -2;
    }

    // Delete from manager file
    int temp_fd = open("./data/manager.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) { flock(mgr_fd, LOCK_UN); close(mgr_fd); return -1; }

    lseek(mgr_fd, 0, SEEK_SET); // Rewind
    struct manager mgr_read;
    while (read(mgr_fd, &mgr_read, sizeof(struct manager)) == sizeof(struct manager)) {
        if (mgr_read.managerID != manager_id) {
            write(temp_fd, &mgr_read, sizeof(struct manager));
        }
    }
    
    flock(mgr_fd, LOCK_UN);
    close(mgr_fd);
    close(temp_fd);

    remove("./data/manager.data");
    rename("./data/manager.tmp", "./data/manager.data");
    
    return 0;
}


int change_admin_password(int admin_id, const char *new_password) {
    int file_desc = open("./data/admin.data", O_RDWR);
    if (file_desc == -1) return -1;

    if (flock(file_desc, LOCK_EX) < 0) {
        close(file_desc);
        return -1;
    }

    struct admin temp_admin;
    int found = 0;
    off_t current_pos;

    while (read(file_desc, &temp_admin, sizeof(struct admin)) == sizeof(struct admin)) {
        if (temp_admin.adminID == admin_id) {
            current_pos = lseek(file_desc, -((long)sizeof(struct admin)), SEEK_CUR);
            
            strncpy(temp_admin.password, new_password, sizeof(temp_admin.password) - 1);
            temp_admin.password[sizeof(temp_admin.password) - 1] = '\0';
            
            lseek(file_desc, current_pos, SEEK_SET);
            if (write(file_desc, &temp_admin, sizeof(struct admin)) == sizeof(struct admin)) {
                found = 1;
            }
            break;
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    return found ? 0 : -1;
}


void handle_admin_requests(int client_socket_fd, int admin_id) {
    char command[RESPONSE_BUFFER_SIZE];
    char response[RESPONSE_BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = read(client_socket_fd, command, sizeof(command) - 1)) > 0) {
        command[bytes_read] = '\0';
        response[0] = '\0';

        if (strcmp(command, "ADD_EMPLOYEE") == 0) {
            char empid_str[20], fname[50], lname[50], pwd[50];
            bytes_read = read(client_socket_fd, empid_str, sizeof(empid_str) - 1); empid_str[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, fname, sizeof(fname) - 1); fname[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, lname, sizeof(lname) - 1); lname[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, pwd, sizeof(pwd) - 1); pwd[bytes_read] = '\0';
            
            empid_str[strcspn(empid_str, "\n")] = 0;
            fname[strcspn(fname, "\n")] = 0;
            lname[strcspn(lname, "\n")] = 0;
            pwd[strcspn(pwd, "\n")] = 0;

            int status = add_employee(atoi(empid_str), fname, lname, pwd);
            if (status == 0) {
                snprintf(response, sizeof(response), "Employee added successfully.");
            } else if (status == -2) {
                snprintf(response, sizeof(response), "Error: Employee ID %s already exists.", empid_str);
            } else {
                snprintf(response, sizeof(response), "Error: Failed to add employee.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "MANAGE_CUSTOMER") == 0) {
            // This is identical to the employee's handler.
            char id_str[20];
            bytes_read = read(client_socket_fd, id_str, sizeof(id_str) - 1); id_str[bytes_read] = '\0';
            id_str[strcspn(id_str, "\n")] = 0;
            int cust_id = atoi(id_str);

            struct customer cust;
            if (get_customer_details(cust_id, &cust) != 0) {
                snprintf(response, sizeof(response), "Error: Customer with ID %d not found.", cust_id);
                write(client_socket_fd, response, strlen(response));
                
                // Read and discard sub-choice
                read(client_socket_fd, id_str, sizeof(id_str) -1);
                snprintf(response, sizeof(response), "Error: Action cancelled.");
                write(client_socket_fd, response, strlen(response));
                continue;
            }

            snprintf(response, sizeof(response), "ID: %d | Name: %s %s | Status: %s | Balance: %.2f | Loan: %.2f",
                     cust.userID, cust.firstName, cust.lastName, cust.status, cust.balance, cust.loan);
            write(client_socket_fd, response, strlen(response));

            char choice_str[10];
            bytes_read = read(client_socket_fd, choice_str, sizeof(choice_str) - 1); choice_str[bytes_read] = '\0';
            int sub_choice = atoi(choice_str);

            if (sub_choice == 1) { // Modify
                char fname[50], lname[50], balance_str[20], loan_str[20], status[20];
                bytes_read = read(client_socket_fd, fname, sizeof(fname) - 1); fname[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, lname, sizeof(lname) - 1); lname[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, balance_str, sizeof(balance_str) - 1); balance_str[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, loan_str, sizeof(loan_str) - 1); loan_str[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, status, sizeof(status) - 1); status[bytes_read] = '\0';

                strncpy(cust.firstName, fname, sizeof(cust.firstName) - 1);
                cust.firstName[strcspn(cust.firstName, "\n")] = 0;
                strncpy(cust.lastName, lname, sizeof(cust.lastName) - 1);
                cust.lastName[strcspn(cust.lastName, "\n")] = 0;
                cust.balance = atof(balance_str);
                cust.loan = atof(loan_str);
                strncpy(cust.status, status, sizeof(cust.status) - 1);
                cust.status[strcspn(cust.status, "\n")] = 0;

                if (update_customer_details(cust_id, &cust) == 0) {
                    snprintf(response, sizeof(response), "Customer details updated.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to update details.");
                }
            } else if (sub_choice == 2) { // Delete
                if (delete_customer(cust_id) == 0) {
                    snprintf(response, sizeof(response), "Customer deleted successfully.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to delete customer.");
                }
            } else if (sub_choice == 3) { // Reset Password
                char new_pass[50];
                bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1); new_pass[bytes_read] = '\0';
                new_pass[strcspn(new_pass, "\n")] = 0;

                if (change_customer_password(cust_id, new_pass) == 0) {
                    snprintf(response, sizeof(response), "Customer password reset.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to reset password.");
                }
            } else {
                snprintf(response, sizeof(response), "Customer management cancelled.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "MANAGE_EMPLOYEE") == 0) {
            char id_str[20];
            bytes_read = read(client_socket_fd, id_str, sizeof(id_str) - 1); id_str[bytes_read] = '\0';
            id_str[strcspn(id_str, "\n")] = 0;
            int emp_id = atoi(id_str);

            struct employee emp;
            if (get_employee_details(emp_id, &emp) != 0) {
                snprintf(response, sizeof(response), "Error: Employee with ID %d not found.", emp_id);
                write(client_socket_fd, response, strlen(response));
                
                // Read and discard sub-choice
                read(client_socket_fd, id_str, sizeof(id_str) -1);
                snprintf(response, sizeof(response), "Error: Action cancelled.");
                write(client_socket_fd, response, strlen(response));
                continue;
            }

            snprintf(response, sizeof(response), "ID: %d | Name: %s %s | Status: %s | Loans: %d",
                     emp.employeeID, emp.first_name, emp.last_name, emp.status, emp.loan_count);
            write(client_socket_fd, response, strlen(response));

            char choice_str[10];
            bytes_read = read(client_socket_fd, choice_str, sizeof(choice_str) - 1); choice_str[bytes_read] = '\0';
            int sub_choice = atoi(choice_str);

            if (sub_choice == 1) { // Modify
                char fname[50], lname[50], status[20];
                bytes_read = read(client_socket_fd, fname, sizeof(fname) - 1); fname[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, lname, sizeof(lname) - 1); lname[bytes_read] = '\0';
                bytes_read = read(client_socket_fd, status, sizeof(status) - 1); status[bytes_read] = '\0';

                strncpy(emp.first_name, fname, sizeof(emp.first_name) - 1);
                emp.first_name[strcspn(emp.first_name, "\n")] = 0;
                strncpy(emp.last_name, lname, sizeof(emp.last_name) - 1);
                emp.last_name[strcspn(emp.last_name, "\n")] = 0;
                strncpy(emp.status, status, sizeof(emp.status) - 1);
                emp.status[strcspn(emp.status, "\n")] = 0;

                if (update_employee_details(emp_id, &emp) == 0) {
                    snprintf(response, sizeof(response), "Employee details updated.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to update details.");
                }
            } else if (sub_choice == 2) { // Delete
                if (delete_employee(emp_id) == 0) {
                    snprintf(response, sizeof(response), "Employee deleted successfully.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to delete employee.");
                }
            } else if (sub_choice == 3) { // Reset Password
                char new_pass[50];
                bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1); new_pass[bytes_read] = '\0';
                new_pass[strcspn(new_pass, "\n")] = 0;

                if (change_employee_password(emp_id, new_pass) == 0) {
                    snprintf(response, sizeof(response), "Employee password reset.");
                } else {
                    snprintf(response, sizeof(response), "Error: Failed to reset password.");
                }
            } else {
                snprintf(response, sizeof(response), "Employee management cancelled.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "MANAGE_USER_ROLES") == 0) {
            char id_str[20], new_role[20];
            bytes_read = read(client_socket_fd, id_str, sizeof(id_str) - 1); id_str[bytes_read] = '\0';
            bytes_read = read(client_socket_fd, new_role, sizeof(new_role) - 1); new_role[bytes_read] = '\0';
            id_str[strcspn(id_str, "\n")] = 0;
            new_role[strcspn(new_role, "\n")] = 0;
            
            if (strcasecmp(new_role, "Manager") == 0) {
                int status = promote_to_manager(atoi(id_str));
                if (status == 0) {
                    snprintf(response, sizeof(response), "User %s promoted to Manager.", id_str);
                } else if (status == -2) {
                    snprintf(response, sizeof(response), "Error: A manager with ID %s already exists.", id_str);
                } else {
                    snprintf(response, sizeof(response), "Error: Could not promote. User not found as employee.");
                }
            } else if (strcasecmp(new_role, "Employee") == 0) {
                int status = demote_to_employee(atoi(id_str));
                if (status == 0) {
                    snprintf(response, sizeof(response), "User %s demoted to Employee.", id_str);
                } else if (status == -2) {
                    snprintf(response, sizeof(response), "Error: An employee with ID %s already exists.", id_str);
                } else {
                    snprintf(response, sizeof(response), "Error: Could not demote. User not found as manager.");
                }
            } else {
                snprintf(response, sizeof(response), "Error: Invalid role specified. Use 'Manager' or 'Employee'.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "CHANGE_PWD") == 0) {
            char new_pass[50];
            bytes_read = read(client_socket_fd, new_pass, sizeof(new_pass) - 1); new_pass[bytes_read] = '\0';
            new_pass[strcspn(new_pass, "\n")] = 0;
            
            if (change_admin_password(admin_id, new_pass) == 0) {
                snprintf(response, sizeof(response), "Password changed successfully.");
            } else {
                snprintf(response, sizeof(response), "Password change failed.");
            }
            write(client_socket_fd, response, strlen(response));
        
        } else if (strcmp(command, "LOGOUT") == 0) {
            snprintf(response, sizeof(response), "Logging out admin %d...", admin_id);
            write(client_socket_fd, response, strlen(response));
            return; // Exit loop
        }
    }
}


// Main Server Functions


int validate_login(const char *role, const char *id_str, const char *password) {
    
    // Check if user is already logged in
    if (strcmp(role, "customer") == 0 || strcmp(role, "employee") == 0 || strcmp(role, "manager") == 0) {
        if (is_user_logged_in(role, atoi(id_str))) {
            return 2; // New status code for "Already Logged In"
        }
    }
   

    int id = atoi(id_str);
    char filename[100];
    void *record_buffer;
    size_t record_size;
    int id_to_check = 0;
    char *pass_to_check = NULL;
    char *status_to_check = NULL; //checking "Active" status

    if (strcmp(role, "customer") == 0) {
        snprintf(filename, sizeof(filename), "./data/customer.data");
        record_size = sizeof(struct customer);
        record_buffer = malloc(record_size);
    } else if (strcmp(role, "employee") == 0) {
        snprintf(filename, sizeof(filename), "./data/employee.data");
        record_size = sizeof(struct employee);
        record_buffer = malloc(record_size);
    } else if (strcmp(role, "manager") == 0) {
        snprintf(filename, sizeof(filename), "./data/manager.data");
        record_size = sizeof(struct manager);
        record_buffer = malloc(record_size);
    } else if (strcmp(role, "admin") == 0) {
        snprintf(filename, sizeof(filename), "./data/admin.data");
        record_size = sizeof(struct admin);
        record_buffer = malloc(record_size);
    } else {
        return 0; // Invalid role
    }

    int file_desc = open(filename, O_RDONLY);
    if (file_desc < 0) {
        perror("File open error during login validation");
        free(record_buffer);
        return 0; // File doesn't exist
    }

    if (flock(file_desc, LOCK_SH) < 0) {
        perror("Error locking file for validation");
        close(file_desc);
        free(record_buffer);
        return 0;
    }

    int login_success = 0;
    while (read(file_desc, record_buffer, record_size) == record_size) {
        if (strcmp(role, "customer") == 0) {
            id_to_check = ((struct customer *)record_buffer)->userID;
            pass_to_check = ((struct customer *)record_buffer)->password;
            status_to_check = ((struct customer *)record_buffer)->status;
        } else if (strcmp(role, "employee") == 0) {
            id_to_check = ((struct employee *)record_buffer)->employeeID;
            pass_to_check = ((struct employee *)record_buffer)->password;
            status_to_check = ((struct employee *)record_buffer)->status;
        } else if (strcmp(role, "manager") == 0) {
            id_to_check = ((struct manager *)record_buffer)->managerID;
            pass_to_check = ((struct manager *)record_buffer)->password;
            status_to_check = "Active"; // Managers are always active
        } else if (strcmp(role, "admin") == 0) {
            id_to_check = ((struct admin *)record_buffer)->adminID;
            pass_to_check = ((struct admin *)record_buffer)->password;
            status_to_check = "Active"; // Admins are always active
        }

        if (id_to_check == id && strcmp(pass_to_check, password) == 0) {
            if (status_to_check != NULL && strcmp(status_to_check, "Active") != 0) {
                login_success = -1; // Account is inactive
            } else {
                login_success = 1; // Correct login
            }
            break; // Found user
        }
    }

    flock(file_desc, LOCK_UN);
    close(file_desc);
    free(record_buffer);
    return login_success;
}


void handle_client_connection(int client_socket_fd) {
    char role_buffer[20], id_pass_buffer[100], id_str[50], password[50];
    char response[100];
    int bytes_read;

    // 1. Reading the Role
    bytes_read = read(client_socket_fd, role_buffer, sizeof(role_buffer) - 1);
    if (bytes_read <= 0) {
        close(client_socket_fd);
        return; // Client disconnected
    }
    role_buffer[bytes_read] = '\0';

    // 2. Reading the "ID Password" string
    bytes_read = read(client_socket_fd, id_pass_buffer, sizeof(id_pass_buffer) - 1);
    if (bytes_read <= 0) {
        close(client_socket_fd);
        return; // Client disconnected
    }
    id_pass_buffer[bytes_read] = '\0';
    sscanf(id_pass_buffer, "%s %s", id_str, password);

    // 3. Validating the Login
    int login_status = validate_login(role_buffer, id_str, password);
    
    
    if (login_status == 2) { // user is Already Logged In
        printf("Login failed for %s: %s (Already logged in)\n", role_buffer, id_str);
        snprintf(response, sizeof(response), "Login Failed: This user is already logged in.");
        write(client_socket_fd, response, strlen(response));
    
    } else if (login_status == 1) { 
        printf("Login successful for %s: %s\n", role_buffer, id_str);
        snprintf(response, sizeof(response), "SUCCESS");
        write(client_socket_fd, response, strlen(response));

        
        if (strcmp(role_buffer, "customer") == 0 || strcmp(role_buffer, "employee") == 0 || strcmp(role_buffer, "manager") == 0) {
            register_session(role_buffer, atoi(id_str));
        }
        
        //handler functions will be implemented now based on the role selected and if the login is succ.
        if (strcmp(role_buffer, "customer") == 0) {
            handle_customer_requests(client_socket_fd, atoi(id_str));
        } else if (strcmp(role_buffer, "employee") == 0) {
            handle_employee_requests(client_socket_fd, atoi(id_str));
        } else if (strcmp(role_buffer, "manager") == 0) {
            handle_manager_requests(client_socket_fd, atoi(id_str));
        } else if (strcmp(role_buffer, "admin") == 0) {
            handle_admin_requests(client_socket_fd, atoi(id_str));
        }

        // This runs on logout OR disconnect
        if (strcmp(role_buffer, "customer") == 0 || strcmp(role_buffer, "employee") == 0 || strcmp(role_buffer, "manager") == 0) {
            deregister_session(role_buffer, atoi(id_str));
        }

    } else if (login_status == -1) { 
        printf("Login failed for %s: %s (Account Inactive)\n", role_buffer, id_str);
        snprintf(response, sizeof(response), "Login Failed: Your account is inactive.");
        write(client_socket_fd, response, strlen(response));
    
    } else { // Fail case
        printf("Login failed for %s: %s\n", role_buffer, id_str);
        snprintf(response, sizeof(response), "Invalid ID or Password or Role");
        write(client_socket_fd, response, strlen(response));
    }
  

    // 5. Close socket when handler returns (on logout or disconnect)
    close(client_socket_fd);
}


void signal_handler(int sig) {
    // Waiting for any child process
    while (waitpid(-1, NULL, WNOHANG) > 0);
}


int main() {
    int server_socket_fd, client_socket_fd;
    struct sockaddr_in server_address, client_address;
    socklen_t client_addr_len;
    pid_t child_pid;

    // for taking care of zombie processes
    signal(SIGCHLD, signal_handler);

    // Creating a server socket
    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    
    int opt = 1;
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
    }

    // Bind socket
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(SERVER_PORT);

    if (bind(server_socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Bind failed");
        close(server_socket_fd);
        exit(EXIT_FAILURE);
    }

    // Listen call
    if (listen(server_socket_fd, 10) < 0) {
        perror("Listen failed");
        close(server_socket_fd);
        exit(EXIT_FAILURE);
    }

    printf("Banking server (fork model) is active on port %d...\n", SERVER_PORT);

    // Accept call
    while (1) {
        client_addr_len = sizeof(client_address);
        client_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_address, &client_addr_len);
        
        if (client_socket_fd < 0) {
            perror("Accept failed");
            continue; // Go back to listening
        }

        // Fork a new process to handle this client
        child_pid = fork();

        if (child_pid < 0) {
            perror("Fork failed");
            close(client_socket_fd);
        } else if (child_pid == 0) {
            
            printf("Child process %d handling new client.\n", getpid());
            close(server_socket_fd); // Child doesn't need the listener socket
            handle_client_connection(client_socket_fd);
            printf("Child process %d finishing.\n", getpid());
            exit(0); // Child process exiting
        } else {
            
            close(client_socket_fd);
        }
    }

    close(server_socket_fd);
    return 0;
}

