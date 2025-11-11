#ifndef EMPLOYEE
#define EMPLOYEE

struct employee {
    int employeeID;           
    char first_name[30];        
    char last_name[30];       
    char password[50];          
    char status[20];           
    int assigned_loans[10];     
    int loan_count;             
};


#endif
