#ifndef FEEDBACK
#define FEEDBACK

struct feedback {
    int feedbackID;
    int customerID;
    char message[255];
    char status[20];  // "Reviewed", "Pending"
};

#endif
