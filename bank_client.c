#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SERVER_PORT 8080
#define RESPONSE_BUFFER_SIZE 1024

// function declarations
void show_main_menu(int socket_fd);
void perform_login(int socket_fd, const char *user_role);
void show_customer_menu(int socket_fd);
void show_employee_menu(int socket_fd);
void show_manager_menu(int socket_fd);
void show_admin_menu(int socket_fd);
void send_request(int socket_fd, const char *request);
void receive_response(int socket_fd, char *buffer, size_t buffer_size);
void receive_multiline_response(int socket_fd);


void send_request(int socket_fd, const char *request) {
    if (write(socket_fd, request, strlen(request)) < 0) {
        perror("Failed to send data to server");
    }
}


void receive_response(int socket_fd, char *buffer, size_t buffer_size) {
    memset(buffer, 0, buffer_size); // Clear the buffer
    int bytes_read = read(socket_fd, buffer, buffer_size - 1);
    if (bytes_read <= 0) { 
        // If read returns 0, the server closed the connection (e.g., on logout)
        strncpy(buffer, "Connection closed by server.", buffer_size - 1);
        return;
    }
    buffer[bytes_read] = '\0'; 
}


void receive_multiline_response(int socket_fd) {
    char buffer[RESPONSE_BUFFER_SIZE];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);

        if (bytes_read <= 0) {
             // Server closed connection (or error)
             printf("Connection lost.\n");
             break;
        }
        buffer[bytes_read] = '\0';

        char *end_marker = strstr(buffer, "END_OF_RESPONSE");
        
        if (end_marker != NULL) {
            *end_marker = '\0'; 
            printf("%s", buffer); 
            break; 
        } else {
            printf("%s", buffer);
        }
    }
}


void show_customer_menu(int socket_fd) {
    int choice;
    char response_buffer[RESPONSE_BUFFER_SIZE];
    char choice_buffer[20]; 

    while (1) {
        printf("\n--- Customer Account Menu ---\n\n\n");
        printf("1. Check Account Balance\n");
        printf("2. Deposit Funds\n");
        printf("3. Withdraw Funds\n");
        printf("4. Transfer Money\n");
        printf("5. Apply for a Loan\n");
        printf("6. Change Your Password\n");
        printf("7. Submit Feedback\n");
        printf("8. View Transaction History\n");
        printf("9. Logout\n");
        printf("10. Exit Application\n");
        printf("Enter your choice (1-10): ");

        
        if (fgets(choice_buffer, sizeof(choice_buffer), stdin) == NULL) {
            printf("\nInput error. Exiting.\n");
            close(socket_fd);
            exit(0); 
        }

        if (choice_buffer[0] == '\n' || choice_buffer[0] == '\0') {
            printf("Invalid input. Please enter a valid choice number.\n");
            continue;
        }

        if (sscanf(choice_buffer, "%d", &choice) != 1) {
            printf("Invalid input. Please enter a valid choice number.\n");
            continue;
        }
        

        switch (choice) {
            case 1: // View Balance
                send_request(socket_fd, "VIEW_BALANCE");
                receive_response(socket_fd, response_buffer, sizeof(response_buffer));
                printf("Your current balance is: %s\n", response_buffer);
                break;

            case 2: // Deposit Money
                send_request(socket_fd, "DEPOSIT_MONEY");
                printf("Enter amount to deposit: ");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                send_request(socket_fd, response_buffer); // Sending the amount
                receive_response(socket_fd, response_buffer, sizeof(response_buffer)); 
                printf("%s\n", response_buffer);
                break;

            case 3: // Withdraw Money
                send_request(socket_fd, "WITHDRAW_MONEY");
                printf("Enter amount to withdraw: ");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                send_request(socket_fd, response_buffer); // Sending the amount
                receive_response(socket_fd, response_buffer, sizeof(response_buffer)); 
                printf("%s\n", response_buffer);
                break;

            case 4: // Transfer Funds
                send_request(socket_fd, "TRANSFER_FUNDS");
                printf("Enter destination User ID: ");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                send_request(socket_fd, response_buffer); 

                printf("Enter amount to transfer: ");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                send_request(socket_fd, response_buffer); 

                receive_response(socket_fd, response_buffer, sizeof(response_buffer)); 
                printf("%s\n", response_buffer);
                break;

            case 5: // Apply for Loan
                send_request(socket_fd, "APPLY_LOAN");
                printf("Enter desired loan amount: ");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                send_request(socket_fd, response_buffer); // Sending the amount
                receive_response(socket_fd, response_buffer, sizeof(response_buffer));
                printf("%s\n", response_buffer);
                break;

            case 6: // Change Password
                send_request(socket_fd, "CHANGE_PWD");
                printf("Enter new password: ");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                response_buffer[strcspn(response_buffer, "\n")] = 0; // Remove trailing newline
                send_request(socket_fd, response_buffer);
                receive_response(socket_fd, response_buffer, sizeof(response_buffer));
                printf("%s\n", response_buffer);
                break;

            case 7: // Submit Feedback
                send_request(socket_fd, "FEEDBACK");
                printf("Enter your feedback:\n");
                fgets(response_buffer, sizeof(response_buffer), stdin);
                response_buffer[strcspn(response_buffer, "\n")] = 0; 
                send_request(socket_fd, response_buffer);
                receive_response(socket_fd, response_buffer, sizeof(response_buffer));
                printf("%s\n", response_buffer);
                break;

            case 8: // View Transaction History
                send_request(socket_fd, "VIEW_TRANSACTION");
                printf("--- Your Transaction History ---\n\n\n");
                receive_multiline_response(socket_fd);
                printf("--- End of History ---\n");
                break;

            case 9: // Logout
                send_request(socket_fd, "LOGOUT");
                receive_response(socket_fd, response_buffer, sizeof(response_buffer));
                printf("%s\n", response_buffer);
                return; 

            case 10: // Exit
                printf("Thank you for using our bank.\nLogging out.\n");
                close(socket_fd);
                exit(0);

            default:
                printf("Invalid choice. Please select a no. from 1-10.\n");
        }
    }
}


void show_employee_menu(int socket_fd) {
    int choice;
    char buffer[RESPONSE_BUFFER_SIZE];
    char choice_buffer[20]; 

    while (1) {
        printf("\n--- Employee Operations Menu ---\n\n\n");
        printf("1. Add New Customer\n");
        printf("2. Manage Customer Account\n");
        printf("3. View Assigned Loan Applications\n");
        printf("4. Approve/Reject Loan\n");
        printf("5. View Customer Transaction History\n");
        printf("6. Change Your Password\n");
        printf("7. Logout\n");
        printf("8. Exit Application\n");
        printf("Enter your choice (1-8): ");

        if (fgets(choice_buffer, sizeof(choice_buffer), stdin) == NULL) {
            printf("\nInput error. Exiting.\n");
            close(socket_fd);
            exit(0);
        }
        if (choice_buffer[0] == '\n' || choice_buffer[0] == '\0') {
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        if (sscanf(choice_buffer, "%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        
        switch (choice) {
            case 1: // Add Customer
                send_request(socket_fd, "ADD_CUSTOMER");
                char userid[20], fname[50], lname[50], pwd[50], balance[20];

                printf("Enter new Customer User ID: ");
                fgets(userid, sizeof(userid), stdin);
                send_request(socket_fd, userid);

                printf("Enter First Name: ");
                fgets(fname, sizeof(fname), stdin);
                send_request(socket_fd, fname);

                printf("Enter Last Name: ");
                fgets(lname, sizeof(lname), stdin);
                send_request(socket_fd, lname);

                printf("Enter Initial Password: ");
                fgets(pwd, sizeof(pwd), stdin);
                send_request(socket_fd, pwd);

                printf("Enter Initial Deposit Amount: ");
                fgets(balance, sizeof(balance), stdin);
                send_request(socket_fd, balance);

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 2: // Manage Customer
                send_request(socket_fd, "MANAGE_CUSTOMER");
                printf("Enter Customer ID to manage: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Customer ID

                receive_response(socket_fd, buffer, sizeof(buffer));
                if (strncmp(buffer, "Error:", 6) == 0) {
                    printf("%s\n", buffer); 
                    break;
                }

                printf("\nCurrent Customer Details:\n%s\n", buffer);
                printf("Select an action:\n");
                printf("  1. Modify Customer Details\n");
                printf("  2. Delete Customer Account\n");
                printf("  3. Reset Customer Password\n");
                printf("  4. Cancel\n");
                printf("Enter choice (1-4): ");
                
                char sub_choice_buffer[20];
                int sub_choice;
                if (fgets(sub_choice_buffer, sizeof(sub_choice_buffer), stdin) == NULL) {
                    printf("\nInput error. Returning to menu.\n");
                    send_request(socket_fd, "4"); 
                    receive_response(socket_fd, buffer, sizeof(buffer)); 
                    continue;
                }
                if (sscanf(sub_choice_buffer, "%d", &sub_choice) != 1) {
                    printf("Invalid input. Returning to menu.\n");
                    send_request(socket_fd, "4"); 
                    receive_response(socket_fd, buffer, sizeof(buffer)); 
                    continue;
                }
                
                sprintf(buffer, "%d", sub_choice);
                send_request(socket_fd, buffer); 

                if (sub_choice == 1) { // Modify
                    printf("Enter new First Name: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);

                    printf("Enter new Last Name: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);

                    printf("Enter new Balance: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                    
                    printf("Enter new Loan Amount: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                    
                    printf("Enter new Status (e.g., Active, Inactive): ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                } else if (sub_choice == 3) { // Reset Password
                    printf("Enter new Password: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                }
                
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 3: // View Assigned Loans
                send_request(socket_fd, "VIEW_LOAN_APPL");
                printf("--- Assigned Loan Applications ---\n\n\n");
                receive_multiline_response(socket_fd);
                printf("--- End of List ---\n");
                break;

            case 4: // Approve/Reject Loan
                send_request(socket_fd, "APP/REJ_LOANS");
                printf("Enter Loan ID (which is the Customer's User ID) to process: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Loan ID

                printf("Enter new status (Approved/Rejected): ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Status

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 5: // View Customer Transactions
                send_request(socket_fd, "VIEW_CUST_TRANS");
                printf("Enter Customer ID to view history: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Customer ID

                printf("--- Transaction History ---\n\n\n");
                receive_multiline_response(socket_fd);
                printf("--- End of History ---\n");
                break;

            case 6: // Change Password
                send_request(socket_fd, "CHANGE_PWD");
                printf("Enter new password: ");
                fgets(buffer, sizeof(buffer), stdin);
                buffer[strcspn(buffer, "\n")] = 0; 
                send_request(socket_fd, buffer);
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;
            
            case 7: // Logout
                send_request(socket_fd, "LOGOUT");
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                return; 

            case 8: // Exit
                printf("Logging out as Employee and exiting the program.\n");
                close(socket_fd);
                exit(0);

            default:
                printf("Invalid choice. Please select from 1-8.\n");
        }
    }
}


void show_manager_menu(int socket_fd) {
    int choice;
    char buffer[RESPONSE_BUFFER_SIZE];
    char choice_buffer[20]; 

    while (1) {
        printf("\n--- Manager Operations Menu ---\n\n\n");
        printf("1. Activate/Deactivate Customer Account\n");
        printf("2. Assign Loan Application to Employee\n");
        printf("3. Review Customer Feedback\n");
        printf("4. Change Your Password\n");
        printf("5. Logout\n");
        printf("6. Exit Application\n");
        printf("Enter your choice (1-6): ");

        if (fgets(choice_buffer, sizeof(choice_buffer), stdin) == NULL) {
            printf("\nInput error. Exiting.\n");
            close(socket_fd);
            exit(0);
        }
        if (choice_buffer[0] == '\n' || choice_buffer[0] == '\0') {
            printf("Invalid input. Please enter a valid choice number.\n");
            continue;
        }
        if (sscanf(choice_buffer, "%d", &choice) != 1) {
            printf("Invalid input. Please enter a valid choice number.\n");
            continue;
        }

        switch (choice) {
            case 1: // Activate/Deactivate Customer
                send_request(socket_fd, "ACT/DEACT_CUST");
                printf("Enter Customer ID: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Customer ID

                printf("Enter action (ACTIVATE/DEACTIVATE): ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Action

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 2: // Assign Loan
                send_request(socket_fd, "ASSIGN_LOAN");
                printf("Enter Customer ID: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Sending Loan ID

                printf("Enter Employee ID to assign loan to: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Sending Employee ID

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 3: // View Feedback
                send_request(socket_fd, "VIEW_FEEDBACK");
                printf("--- Pending Customer Feedback ---\n\n");
                receive_multiline_response(socket_fd);
                printf("--- End of Feedback ---\n");
                
                printf("\nEnter Feedback ID to mark as 'Reviewed' (enter 0 to skip): ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send Feedback ID

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 4: // Change Password
                send_request(socket_fd, "CHANGE_PWD");
                printf("Enter the new password: ");
                fgets(buffer, sizeof(buffer), stdin);
                buffer[strcspn(buffer, "\n")] = 0; 
                send_request(socket_fd, buffer);
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 5: // Logout
                send_request(socket_fd, "LOGOUT");
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                return; 

            case 6: // Exit
                printf("Logging out as Manager.\n");
                close(socket_fd);
                exit(0);

            default:
                printf("Invalid choice. Please select from 1-6.\n");
        }
    }
}


void show_admin_menu(int socket_fd) {
    int choice;
    char buffer[RESPONSE_BUFFER_SIZE];
    char choice_buffer[20]; 

    while (1) {
        printf("\n--- Administrator Menu ---\n\n\n");
        printf("1. Add New Bank Employee\n");
        printf("2. Manage Customer Account\n");
        printf("3. Manage Employee Account\n");
        printf("4. Manage User Roles (Promote/Demote)\n");
        printf("5. Change Your Password\n");
        printf("6. Logout\n");
        printf("7. Exit Application\n");
        printf("Enter your choice (1-7): ");

        
        if (fgets(choice_buffer, sizeof(choice_buffer), stdin) == NULL) {
            printf("\nInput error. Exiting.\n");
            close(socket_fd);
            exit(0);
        }
        if (choice_buffer[0] == '\n' || choice_buffer[0] == '\0') {
            printf("Invalid input. Please enter a valid choice number.\n");
            continue;
        }
        if (sscanf(choice_buffer, "%d", &choice) != 1) {
            printf("Invalid input. Please enter a valid choice number.\n");
            continue;
        }
        
        switch (choice) {
            case 1: // Add Employee
                send_request(socket_fd, "ADD_EMPLOYEE");
                char empid[20], fname[50], lname[50], pwd[50];

                printf("Enter new Employee ID: ");
                fgets(empid, sizeof(empid), stdin);
                send_request(socket_fd, empid);

                printf("Enter First Name: ");
                fgets(fname, sizeof(fname), stdin);
                send_request(socket_fd, fname);

                printf("Enter Last Name: ");
                fgets(lname, sizeof(lname), stdin);
                send_request(socket_fd, lname);

                printf("Enter Initial Password: ");
                fgets(pwd, sizeof(pwd), stdin);
                send_request(socket_fd, pwd);

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 2: // Manage Customer
                send_request(socket_fd, "MANAGE_CUSTOMER");
                printf("Enter Customer ID to manage: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); 

                receive_response(socket_fd, buffer, sizeof(buffer));
                if (strncmp(buffer, "Error:", 6) == 0) {
                    printf("%s\n", buffer); 
                    break;
                }

                printf("\nCurrent Customer Details:\n%s\n", buffer);
                printf("Select an action:\n");
                printf("  1. Modify Customer Details\n");
                printf("  2. Delete Customer Account\n");
                printf("  3. Reset Customer Password\n");
                printf("  4. Cancel\n");
                printf("Enter choice (1-4): ");
                
                
                char sub_choice_buffer_cust[20];
                int sub_choice_cust;
                if (fgets(sub_choice_buffer_cust, sizeof(sub_choice_buffer_cust), stdin) == NULL) {
                    printf("\nInput error. Returning to menu.\n");
                    send_request(socket_fd, "4"); 
                    receive_response(socket_fd, buffer, sizeof(buffer)); 
                    continue;
                }
                if (sscanf(sub_choice_buffer_cust, "%d", &sub_choice_cust) != 1) {
                    printf("Invalid input. Returning to menu.\n");
                    send_request(socket_fd, "4"); 
                    receive_response(socket_fd, buffer, sizeof(buffer)); 
                    continue;
                }
                

                sprintf(buffer, "%d", sub_choice_cust);
                send_request(socket_fd, buffer); 

                if (sub_choice_cust == 1) { // Modify
                    printf("Enter new First Name: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);

                    printf("Enter new Last Name: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);

                    printf("Enter new Balance: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                    
                    printf("Enter new Loan Amount: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                    
                    printf("Enter new Status (e.g., Active, Inactive): ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                } else if (sub_choice_cust == 3) { // Reset Password
                    printf("Enter new Password: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                }
                
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;
            
            case 3: // Manage Employee
                send_request(socket_fd, "MANAGE_EMPLOYEE");
                printf("Enter Employee ID to manage: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); 

                receive_response(socket_fd, buffer, sizeof(buffer));
                if (strncmp(buffer, "Error:", 6) == 0) {
                    printf("%s\n", buffer); 
                    break;
                }

                printf("\nCurrent Employee Details:\n%s\n", buffer);
                printf("Select an action:\n");
                printf("  1. Modify Employee Details\n");
                printf("  2. Delete Employee Account\n");
                printf("  3. Reset Employee Password\n");
                printf("  4. Cancel\n");
                printf("Enter choice (1-4): ");
                
                char sub_choice_buffer_emp[20];
                int sub_choice_emp;
                if (fgets(sub_choice_buffer_emp, sizeof(sub_choice_buffer_emp), stdin) == NULL) {
                    printf("\nInput error. Returning to menu.\n");
                    send_request(socket_fd, "4"); 
                    receive_response(socket_fd, buffer, sizeof(buffer)); 
                    continue;
                }
                if (sscanf(sub_choice_buffer_emp, "%d", &sub_choice_emp) != 1) {
                    printf("Invalid input. Returning to menu.\n");
                    send_request(socket_fd, "4"); 
                    receive_response(socket_fd, buffer, sizeof(buffer)); 
                    continue;
                }

                sprintf(buffer, "%d", sub_choice_emp);
                send_request(socket_fd, buffer); 

                if (sub_choice_emp == 1) { // Modify
                    printf("Enter new First Name: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);

                    printf("Enter new Last Name: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                    
                    printf("Enter new Status (e.g., Active, OnLeave): ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                } else if (sub_choice_emp == 3) { // Reset Password
                    printf("Enter new Password: ");
                    fgets(buffer, sizeof(buffer), stdin);
                    send_request(socket_fd, buffer);
                }
                
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;
            
            case 4: // Manage User Roles
                send_request(socket_fd, "MANAGE_USER_ROLES");
                printf("Enter ID of user to promote/demote: ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send User ID

                printf("Enter new role (Manager/Employee): ");
                fgets(buffer, sizeof(buffer), stdin);
                send_request(socket_fd, buffer); // Send New Role

                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 5: // Change Password
                send_request(socket_fd, "CHANGE_PWD");
                printf("Enter new password: ");
                fgets(buffer, sizeof(buffer), stdin);
                buffer[strcspn(buffer, "\n")] = 0; 
                send_request(socket_fd, buffer);
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                break;

            case 6: // Logout
                send_request(socket_fd, "LOGOUT");
                receive_response(socket_fd, buffer, sizeof(buffer));
                printf("%s\n", buffer);
                return; 

            case 7: // Exit
                printf("Logging out as Administrator.\n");
                close(socket_fd);
                exit(0);

            default:
                printf("Invalid choice. Please select from 1-7.\n");
        }
    }
}



void perform_login(int socket_fd, const char *user_role) {
    char id_buffer[50], pass_buffer[50], combined_buffer[100], server_response[RESPONSE_BUFFER_SIZE];

    printf("Enter your User ID: ");
    fgets(id_buffer, sizeof(id_buffer), stdin);
    id_buffer[strcspn(id_buffer, "\n")] = 0; // Remove newline

    printf("Enter your Password: ");
    fgets(pass_buffer, sizeof(pass_buffer), stdin);
    pass_buffer[strcspn(pass_buffer, "\n")] = 0; // Remove newline

    // sends the ID and Password in one request in a specified format - id pwd
    snprintf(combined_buffer, sizeof(combined_buffer), "%s %s", id_buffer, pass_buffer);
    send_request(socket_fd, combined_buffer);

    // Waiting for the server's response SUCCESS or error
    receive_response(socket_fd, server_response, sizeof(server_response));

    if (strcmp(server_response, "SUCCESS") == 0) {
        printf("Login successful! Welcome.\n");
        // Redirect to the correct menu based on role
        if (strcmp(user_role, "customer") == 0) {
            show_customer_menu(socket_fd);
        } else if (strcmp(user_role, "employee") == 0) {
            show_employee_menu(socket_fd);
        } else if (strcmp(user_role, "manager") == 0) {
            show_manager_menu(socket_fd);
        } else if (strcmp(user_role, "admin") == 0) {
            show_admin_menu(socket_fd);
        }
        // When the role menu returns (on logout) this function returns.
    } else {
        printf("Login attempt failed: %s\n", server_response);
    }
}


void show_main_menu(int socket_fd) {
    char role_buffer[20];
    
    printf("\n--- Welcome to the Bank --- \n\n\n");
    printf("Please select your role -\ncustomer\nemployee\nmanager\nadmin\nor type 'exit': ");
    fgets(role_buffer, sizeof(role_buffer), stdin);
    role_buffer[strcspn(role_buffer, "\n")] = 0; 

    if (strcmp(role_buffer, "exit") == 0) {
        printf("Exiting the program.\n");
        close(socket_fd);
        exit(0); // Exiting the program; will have to execute client again
    }

    // Comparing the role entered with the actual roles  
    if (strcmp(role_buffer, "customer") == 0 || strcmp(role_buffer, "employee") == 0 ||
        strcmp(role_buffer, "manager") == 0 || strcmp(role_buffer, "admin") == 0) {
        
        send_request(socket_fd, role_buffer); // Sending the request to server with the selected role
        perform_login(socket_fd, role_buffer); // logging in now
    
    } else {  // if perform_login returns (after a logout or failed login) this function returns to main()
        printf("Invalid role.\nChoose a role from customer, employee, manager and admin only.\nPlease try again.\n");
    }
    
}


int main() {
    int clientfd;
    struct sockaddr_in server_address;

    while (1) { 
        // Creating the client side socket
        clientfd = socket(AF_INET, SOCK_STREAM, 0);
        if (clientfd < 0) {
            perror("Socket creation failed. Retrying in 5 seconds...");
            sleep(5); // Wait before retrying
            continue;
        }

        //server's address
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(SERVER_PORT);
        
        if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) {
            perror("Invalid server address");
            close(clientfd);
            sleep(5);
            continue;
        }

        // Connecting to the server using connect()
        if (connect(clientfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            perror("Connection to server failed. Retrying in 5 seconds...");
            close(clientfd);
            sleep(5);
            continue; // will retry the connection
        }

        printf("Successfully connected to the banking server!\n");

        // Start the main menu. This function will now return on logout or if the connection is lost.
        show_main_menu(clientfd);

        // When show_main_menu returns, it means the user has logged out or the connection was dropped.
        // Closing the socket ;the loop will create a new connection.
        printf("Logged out or disconnected. Closing connection.\n");
        close(clientfd);
        // The loop will now repeat, starting with a new connection attempt
        printf("\n--- Please login again ---\n\n\n");
    }

    return 0;
}
