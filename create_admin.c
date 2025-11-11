#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "admin.h" // Make sure admin.h is in the same directory

int main() {
    int file_desc;
    struct admin first_admin;

    // Define the first admin's details
    first_admin.adminID =101; // Your chosen admin ID
    strcpy(first_admin.first_name, "admin");
    strcpy(first_admin.last_name, "A");
    strcpy(first_admin.password, "admin"); // Your chosen password

    // --- Create admin.data ---
    // O_TRUNC will overwrite the file if it already exists
    file_desc = open("./data/admin.data", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_desc < 0) {
        perror("Error creating ./data/admin.data");
        return 1;
    }

    // Write the single admin record to the file
    if (write(file_desc, &first_admin, sizeof(struct admin)) != sizeof(struct admin)) {
        perror("Error writing initial admin data");
        close(file_desc);
        return 1;
    }

    close(file_desc);
    printf("Successfully created ./data/admin.data with admin ID 101.\n");
    printf("You can now log in as admin with ID 101 and password 'admin'.\n");

    return 0;
}


