#define _POSIX_C_SOURCE 202009L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "global.h"

#define MAXTHREADS 10 // the amount of threads we want to create
#define QLEN 5 // the queue length length for a singular socket
#define MAX_LENGTH 100 // maximum input/output length, same as in client side

struct BankAccount *accounts = NULL; // empty list of accounts (account number, balance, read/write lock)
int num_accounts; // number of accounts
struct ThreadData thread_data[MAXTHREADS]; // list of threads' data (id, queue size, mutex)
pthread_t threads[MAXTHREADS]; // list of threads to join them later
int bankIsOpen = 1; // to use for graceful shutdown
pthread_mutex_t logM; // logger mutex

void toLog(char *string, pthread_mutex_t mut) { // logging function
    FILE *fp = fopen("log.txt", "a"); // if there is a log file, just append
    if (fp == NULL) { // if append-open failed
        fprintf(stderr, "Error appending to the log file.\n");
        return;
    } else { // otherwise append
        int ret = pthread_mutex_trylock(&mut);
        while (ret == EBUSY) { // try to acquire the lock until it succeeds
            ret = pthread_mutex_trylock(&mut);
        }
        assert(ret == 0);
        fprintf(fp, "%s", string);
        fclose(fp);
        pthread_mutex_unlock(&mut);
    }
}

void saveAccDetails() { // save accounts' details
    FILE *file = fopen("account_details.txt", "w"); // open file in write mode, creates one if it doesn't exist
    if (file == NULL) { // check if it worked
        fprintf(stderr, "Error opening the account details file.\n");
        return;
    }
    int i;
    for (i = 0; i < num_accounts; i++) { // update the information for each account into the file
        fprintf(file, "%d - %d\n", accounts[i].accountN, accounts[i].balance);
    }
    fclose(file);
}

void addAcc(int accN, int balance) { // add a new account into accounts
    accounts = realloc(accounts, (num_accounts + 1) * sizeof(struct BankAccount)); // reallocate memory
    if (accounts == NULL) { // if allocation failed
        fprintf(stderr, "Error adding an account.\n");
        return;
    }
    num_accounts++; // update number of accounts
    accounts[num_accounts - 1].accountN = accN; // save account details
    accounts[num_accounts - 1].balance = balance;
    assert((pthread_rwlock_init(&accounts[num_accounts - 1].lock, NULL)) == 0);
    saveAccDetails();
}

void initAcc() { // initalize accounts
    accounts = (struct BankAccount*)malloc(sizeof(struct BankAccount)); // allocate memory
    if (accounts == NULL) { // if memory allocation failed
        fprintf(stderr, "Error initializing the accounts.\n");
        return;
    }
    num_accounts = 0;
    int acc, amount;
    FILE *file = fopen("account_details.txt", "r"); // check for previous account data
    if (file == NULL) { // treat as file doesn't exist, even though there could just be an opening problem
        return;
    } else {
        while (!feof(file)) { // while we haven't reached the end of the file
            int f;
            assert((f = fscanf(file, "%d - %d\n", &acc, &amount)) != 0); // write the scanned values into variables
            addAcc(acc, amount); // add each found pre-existing account
        }
        fclose(file);
    }
}

void accCheck(int acc) { // check if an account exists and adds one if not
    int counter = 0;
    int i;
    for (i = 0; i < num_accounts; ++i) { // for every account in accounts
        if (accounts[i].accountN != acc) { // increment counter if such a account number wasn't found
            counter++;
        }
    }
    if (counter == num_accounts) { // if counter matches num_accounts, no account had the checked account number
        addAcc(acc, 0); // thus we can add a new one with a balance of 0
    }
}

int findAcc(int accN) { // finds the account index with the account number
    int i;
    for (i = 0; i < num_accounts; ++i) {
        if (accounts[i].accountN == accN) { // if we found the right number
            break;
        }
    }
    return i;
}

void lockR(int accN) { // put a read lock
    pthread_rwlock_t *lock = &accounts[findAcc(accN)].lock;
    int ret = pthread_rwlock_tryrdlock(lock);
    while (ret == EBUSY) { // while a lock exists, try again
        ret = pthread_rwlock_tryrdlock(lock);
    }
    assert(ret == 0);
}

void lockW(int accN) { // put a write lock
    pthread_rwlock_t *lock = &accounts[findAcc(accN)].lock;
    int ret = pthread_rwlock_trywrlock(lock);
    while (ret == EBUSY) { // while a lock exists, try again
        ret = pthread_rwlock_trywrlock(lock);
    }
    assert(ret == 0);
}

void unlock(int accN) { // unlock an account
    assert((pthread_rwlock_unlock(&accounts[findAcc(accN)].lock)) == 0);
}

void handleTrans(char cmd, int acc1, int acc2, int amount, int client_socket) { // handle transactions
    char response[MAX_LENGTH];

    int w;
    switch (cmd) {
        case 'l': // get balance of account acc1
            accCheck(acc1); // always check that the account exists, if not, it is created
            lockR(acc1);
            sprintf(response, "ok: Balance of account %d: %d\n", acc1, accounts[findAcc(acc1)].balance); // move the response to the variable
            assert((w = write(client_socket, response, strlen(response) + 1)) != -1); // respond
            toLog(response, logM);
            unlock(acc1);
            break;
        case 'w': // withdraw from account acc1
            accCheck(acc1); // always check that the account exists, if not, it is created
            lockW(acc1);
            if (accounts[findAcc(acc1)].balance >= amount) { // if there is enough money
                accounts[findAcc(acc1)].balance -= amount;
                sprintf(response, "ok: Withdrew %d from account %d\n", amount, acc1);
                assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
                toLog(response, logM);
            } else { // not enough money
                sprintf(response, "fail: Not enough money on account %d\n", acc1);
                assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
                toLog(response, logM);
            }
            unlock(acc1);
            break;
        case 't': // transfer amount from acc1 to acc2
            accCheck(acc1); // always check that the account exists, if not, it is created
            accCheck(acc2); // always check that the account exists, if not, it is created
            lockW(acc1);
            if (acc1 != acc2) { // so that we dont try to lock the same account twice, causing a forever loop
                lockW(acc2);
            }
            if (accounts[findAcc(acc1)].balance >= amount) { // if there is enough money
                accounts[findAcc(acc1)].balance -= amount;
                accounts[findAcc(acc2)].balance += amount;
                sprintf(response, "ok: Transferred %d from account %d to account %d\n", amount, acc1, acc2);
                assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
                toLog(response, logM);
            } else { // not enough money
                sprintf(response, "fail: Not enough money on account %d\n", acc1);
                assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
                toLog(response, logM);
            }
            unlock(acc1);
            if (acc1 != acc2) {
                unlock(acc2);
            }
            break;
        case 'd': // deposit to account acc1
            accCheck(acc1); // always check that the account exists, if not, it is created
            lockW(acc1);
            accounts[findAcc(acc1)].balance += amount;
            sprintf(response, "ok: Deposited %d to account %d\n", amount, acc1);
            assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
            toLog(response, logM);
            unlock(acc1);
            break;
        case 'q': // quit the desk
            sprintf(response, "ok: Quit the desk\n");
            assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
            toLog(response, logM);
            break;
        default:
            sprintf(response, "fail: Invalid command\n");
            assert((w = write(client_socket, response, strlen(response) + 1)) != -1);
            toLog(response, logM);
            break;
    }

    saveAccDetails(); // update the details after the transaction
}

void copydata(int from,int to) {
  char buf[1024];
  int amount;

  while ((amount = read(from, buf, sizeof(buf))) > 0) {
    assert((write(to, buf, amount) == amount));

    char cmd = buf[0];
    int acc1 = 0, acc2 = 0, amount = 0;
    char *errMsg = "fail: Error in command\n";
    int e;
    buf[strcspn(buf, "\n")] = '\0'; // remove trailing newline

    switch (cmd) { // scan the others
        case 'l': // of form l acc1
            if (sscanf(buf, "l %d", &acc1) == 1 && buf[1] == ' ') { // next char must be always space
                handleTrans(cmd, acc1, acc2, amount, from);
                break;
            } else {
                assert((e = write(from, errMsg, strlen(errMsg) + 1)) != -1);
                break;
            }
        case 'w': // of form w acc1 amount
            if (sscanf(buf, "w %d %d", &acc1, &amount) == 2 && buf[1] == ' ') { // next char must be always space
                handleTrans(cmd, acc1, acc2, amount, from);
                break;
            } else {
                assert((e = write(from, errMsg, strlen(errMsg) + 1)) != -1);
                break;
            }
        case 't': // of form t acc1 acc2 amount
            if (sscanf(buf, "t %d %d %d", &acc1, &acc2, &amount) == 3 && buf[1] == ' ') { // next char must be always space
                handleTrans(cmd, acc1, acc2, amount, from);
                break;
            } else {
                assert((e = write(from, errMsg, strlen(errMsg) + 1)) != -1);
                break; 
            }
        case 'd': // of form d acc1 amount
            if (sscanf(buf, "d %d %d", &acc1, &amount) == 2 && buf[1] == ' ') { // next char must be always space
                handleTrans(cmd, acc1, acc2, amount, from);
                break; 
            } else {
                assert((e = write(from, errMsg, strlen(errMsg) + 1)) != -1);
                break; 
            }
        case 'q': // of form q
            if (strlen(buf) > 1) { // q must be the only letter
                assert((e = write(from, errMsg, strlen(errMsg) + 1)) != -1);
                break;
            } else {
                handleTrans(cmd, acc1, acc2, amount, from);
                break;
            }
        default:
            handleTrans(cmd, acc1, acc2, amount, from);
            break;
    }
  }
  
  assert(amount >= 0);
}

int findSmallestQ() { // find the shortest queue's index
    int minIndex = 0;
    int minQSize = thread_data[0].qSize;

    int i;
    for (i = 0; i < MAXTHREADS; ++i) {
        if (thread_data[i].qSize < minQSize) {
            minIndex = i;
            minQSize = thread_data[i].qSize;
        }
    }
    return minIndex;
}

//thread routine function goes under here...
void *thread_routine(void *arg) {
    struct ThreadData *data = (struct ThreadData*) arg; // the thread data object (desk)
    int client_socket;
    struct sockaddr_un client_addr; // client address
    socklen_t clen = sizeof(client_addr); // client length

    int connIsBank;
    while (1) { // start accepting connections
        client_socket = accept(data->id, (struct sockaddr*) &client_addr, &clen); // accept connections
        int r = read(client_socket, &connIsBank, sizeof(int));
        r = r;
        if (connIsBank) {
            break;
        }

        char *rd = "ready\n";
        assert((write(client_socket, rd, strlen(rd) + 1)) != -1); // tell the client that the desk is ready to serve
        copydata(client_socket, STDOUT_FILENO); // copies to buf while the client hasn't quit with 'q'
        close(client_socket); // close the connection
        pthread_mutex_lock(&(data->mutex));
        data->qSize--; // update queue size
        pthread_mutex_unlock(&(data->mutex));
    }

    char l[19];
    sprintf(l, "Desk %d exiting\n", data->id);
    toLog(l, logM);
    return NULL;
}

void createThreads() { // function that creates all (10) threads
    int i;
    for (i = 0; i < MAXTHREADS; ++i) {
        pthread_mutex_init(&(thread_data[i].mutex), NULL); // initalize every mutex
        thread_data[i].qSize = 0; // set the queue size
        assert((thread_data[i].id = socket(AF_UNIX, SOCK_STREAM, 0)) != -1); // assign a UNIX socket
        char path[14];
        sprintf(path, "unix_socket_%d", i);
        unlink(path); // unlink any previous paths
        thread_data[i].path = path; // set the socket path

        struct sockaddr_un server_addr; // server address
        server_addr.sun_family = AF_UNIX; // initalize the address properties
        sprintf(server_addr.sun_path, "unix_socket_%d", i);
        socklen_t slen = sizeof(server_addr.sun_family) + strlen(server_addr.sun_path); // server length

        assert((bind(thread_data[i].id, (struct sockaddr*) &server_addr, slen)) != -1); // bind the socket
        assert((listen(thread_data[i].id, QLEN)) != -1); // start listening with a queue size of 5 (QLEN)
        assert((pthread_create(&threads[i], NULL, thread_routine, (void *) &thread_data[i])) == 0); // create the thread

        char l[21];
        sprintf(l, "Desk %d is now open\n", i);
        toLog(l, logM);
    }
}

void sigHandler(int sig) { // to handle receiving a SIGINT or SIGTERM
    if (sig == SIGINT || sig == SIGTERM) {
        bankIsOpen = 0;
    }
}

//main function which creates threads and assigns them a thread routine function goes under here...
int main(int argc, char **argv) { // main server starter function
    assert((pthread_mutex_init(&logM, NULL)) == 0);
    FILE *fp = fopen("log.txt", "w"); // try to open or create a log file, starting a blank slate
    if (fp == NULL) { // if failed
        fprintf(stderr, "Error in opening or creating an empty log file.\n");
        return -1;
    }

    struct sigaction signal; // first and foremost, set up the signal handling stuff
    sigemptyset(&signal.sa_mask);
    signal.sa_flags = 0;
    signal.sa_handler = sigHandler;
    assert((sigaction(SIGINT, &signal, NULL)) == 0);
    assert((sigaction(SIGTERM, &signal, NULL)) == 0);

    unlink("unix_socket"); // unlink previous main socket

    toLog("Bank is open\n", logM);
    initAcc(); // figure out the accounts (if any pre-exist or not)
    toLog("Accounts have been initalized\n", logM);
    createThreads(); // create all 10 desk threads + sockets

    int main_socket, client_socket;
    pthread_mutex_t main_mutex;
    struct sockaddr_un main_addr, client_addr;

    main_addr.sun_family = AF_UNIX; // main address setup    
    sprintf(main_addr.sun_path, "unix_socket");
    socklen_t mlen = sizeof(main_addr); // main address length

    assert((main_socket = socket(AF_UNIX, SOCK_STREAM, 0)) != -1); // create main socket
    assert((bind(main_socket, (struct sockaddr*) &main_addr, mlen)) != -1); // bind the main socket
    assert((listen(main_socket, QLEN)) != -1); // start listening on the main socket

    pthread_mutex_init(&main_mutex, NULL); // initialize main mutex
    socklen_t clen = sizeof(client_addr); // client length

    while (1) {
        client_socket = accept(main_socket, (struct sockaddr*) &client_addr, &clen); // accep client connection
        if (!bankIsOpen) {
            break;
        }
        if (client_socket == -1 && errno == EINTR) {
            break;
        }
        pthread_mutex_lock(&main_mutex); // lock the mutex for exclusive access
        int qIdx = findSmallestQ(); // smallest queue's index
        char *path = thread_data[qIdx].path; // smallest queue's path
        assert((write(client_socket, path, strlen(path) + 1)) != -1); // forward the new socket path to the client
        thread_data[qIdx].qSize++; // update the queue size
        pthread_mutex_unlock(&main_mutex); // unlock the mutex
        close(client_socket);
    }

    toLog("Main socket has been closed\n", logM);

    for (int i = 0; i < MAXTHREADS; i++) { // wait for threads to finish (sync up)
        struct sockaddr_un address; // desks are always waiting for a new connection, imply via the bank itself that they can shut down
        int socketDesk;
        assert((socketDesk = socket(AF_UNIX, SOCK_STREAM, 0)) != -1);
        address.sun_family = AF_UNIX;
        char path[14];
        sprintf(path, "unix_socket_%d", i);
        strcpy(address.sun_path, path);
        socklen_t len = sizeof(address.sun_family) + strlen(address.sun_path);
        connect(socketDesk, (struct sockaddr *) &address, len);

        int isBank = 1;
        assert((write(socketDesk, &isBank, sizeof(int))) != -1);

        pthread_join(threads[i], NULL);
        close(socketDesk);
    }
    toLog("All desks have been closed\n", logM);
    free(accounts); // don't forget to free the mallocced accounts

    for (int i = 0; i < MAXTHREADS; i++) { // destroy all mutexes
        pthread_mutex_destroy(&(thread_data[i].mutex));
    }
    pthread_mutex_destroy(&main_mutex);
    toLog("Bank has been closed\n", logM);
    pthread_mutex_destroy(&logM);
    return 0;
}