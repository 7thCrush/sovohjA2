#ifndef GLOBAL
#define GLOBAL

#include <pthread.h>

struct BankAccount {
    int accountN;
    int balance;
    pthread_rwlock_t lock;
};

struct ThreadData {
    int id;
    int qSize;
    pthread_mutex_t mutex;
    char *path;
};

#endif