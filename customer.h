#ifndef CUSTOMER
#define CUSTOMER

struct customer {
    int userID;
    char firstName[30];
    char lastName[30];
    char password[20];
    float balance;
    float loan;
    char status[20];
};

#endif
