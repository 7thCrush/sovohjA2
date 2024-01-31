/*
 * ELEC-C7310 Assignment #2: Threadbank
 * Testbench
 *
 * Single-thread event loop with simple per-client state machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <assert.h>

#include "linebuffer.h"

#define RESPBUFSIZE 256

enum state {
    uninit = 0,
    inqueue,	// Waiting for "ready"
    cmdsent,	// Waiting for "ok"/"fail" response
    exiting	// Waiting for notification of process exiting
};

struct session {
    int fdin,fdout;	// pipes to the client
    enum state state;	// state of client connection
    pid_t pid;		// PID of client process 
    struct linebuf *respbuf; // buffer for reading responses
    char response[RESPBUFSIZE];  // buffer for reading responses
};


/**
 * Initialize a client.
 * \param c Client structure to initialize.
 * \param bin Binary to run for the client.
 * \return 0 on success, -1 on failure. */
int client_init(struct session *c,const char *bin) {
    if (c == NULL) return -1;
    c->state = uninit;
    int pin[2],pout[2];
    if (pipe(pin) < 0) return -1;
    if (pipe(pout) < 0) { close(pin[0]); close(pin[1]); return -1; }
    pid_t pid = fork();
    if (pid < 0) { close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]); return -1; }
    if (pid == 0) {
        close(STDIN_FILENO);
        if (dup(pout[0]) == -1) return -1; // dup() failed? abort.
        close(pout[0]); close(pout[1]);
        close(STDOUT_FILENO);
        if (dup(pin[1]) == -1) return -1; // dup() failed? abort.
        close(pin[1]); close(pin[0]);
        execl(bin,bin,NULL);
        return -1; // exec() failed
    } else {
        c->pid = pid;
        c->fdin = pin[0];
        c->fdout = pout[1];
        c->state = inqueue;
        c->respbuf = linebuf_new();
    }
    return 0;
}

/**
 * Close client and clear the structure.
 * \param c Client structure to close. */
void client_close(struct session *c) {
    assert(c != NULL);
    linebuf_free(c->respbuf);
    if (c->state == uninit) return;  // already uninitialized
    close(c->fdin); c->fdin = -1;
    close(c->fdout); c->fdout = -1;
    c->state = uninit;
    c->pid = -1;
}
/**
 * Identify the client that was reaped and make sure it's closed.
 * \param t Client structure table.
 * \param max Size of client table.
 * \param pid Process ID that terminated.
 * \return 0 on success, -1 on failure. */
int client_reap_pid(struct session *t,int max,int pid) {
    int i;
    for(i=0;i<max;i++) {
        struct session *c = &t[i];
        if (c->state == uninit) continue;
        if (c->pid == pid) {
            client_close(c);
            return 0;
        }
    }
    printf("Couldn't find client with pid %d to reap\n",pid);
    return -1;
}

#define CMDBUFSIZ 20
/**
 * Send a new random command from the client.
 * \param c Client structure.
 * \param i Client index (for printouts only).
 * \return 0 on success, -1 if writing to client failed. */
int client_newcmd(struct session *c,int i) {
    char cmdbuf[CMDBUFSIZ];
    int r = (int)(random() & 7);
    int len = -1;
    switch (r) {
    case 0: // list
    case 1:
        len = snprintf(cmdbuf,CMDBUFSIZ,"l %d\n",(int)random() % 20);
        c->state = cmdsent;
        break;
    case 2: // withdraw
    case 3:
        len = snprintf(cmdbuf,CMDBUFSIZ,"w %d %d\n",(int)random() % 20,(int)random() % 100);
        c->state = cmdsent;
        break;
    case 4: // deposit
    case 5:
        len = snprintf(cmdbuf,CMDBUFSIZ,"d %d %d\n",(int)random() % 20,(int)random() % 100);
        c->state = cmdsent;
        break;
    case 6: // transfer
        len = snprintf(cmdbuf,CMDBUFSIZ,"t %d %d %d\n",(int)random() % 20,(int)random() % 20,(int)random() % 100);
        c->state = cmdsent;
        break;
    case 7: // quit
        len = snprintf(cmdbuf,CMDBUFSIZ,"q\n");
        c->state = exiting;
        break;
    }
    if (len < 0) {
        printf("Error preparing message to the client\n");
        return -1;
    }
    printf("#%d: Sending command: %s",i,cmdbuf);
    if (write(c->fdout,cmdbuf,len) < 0) { // Writing to client failed
        return -1;
    }
    return 0;        
}
/**
 * Send quit command.
 * \param c Client structure.
 * \param i Client index (for printouts only).
 * \return 0 on success, -1 if writing to client failed. */
int client_cmdquit(struct session *c,int i) {
    const char *cmdquit = "q\n";
    c->state = exiting;
    printf("#%d: Sending command: %s",i,cmdquit);
    if (write(c->fdout,cmdquit,strlen(cmdquit)) < 0) { // Writing failed
        return -1;
    }
    return 0;
}

/**
 * Main function.
 * \param argc Argument count.
 * \param argv Argument table.
 * \return 0 on success. */
int main(int argc,char **argv) {
    time_t seed = time(NULL);
    int numtests = 100;
    int maxclients = 10;

    int opt;
    while ((opt = getopt(argc,argv,"c:n:s:")) != -1) {
        switch (opt) {
        case 'n': numtests = atoi(optarg); break;
        case 'c': maxclients = atoi(optarg); break;
        case 's': seed = atoi(optarg); break;
        default: printf("Usage: %s [-c numclients] [-n numtests] [-s seedval] binary\n",argv[0]);
            return -1;
        }
    }
    if (optind >= argc) {
        printf("Missing executable to test\n");
        return -1;
    }
    // Initialize random number generator with seed value
    printf("Random seed = %ld\n",seed);
    srandom(seed);

    // argv[optind] is the first non-option paramete
    char *bin = argv[optind];

    signal(SIGPIPE,SIG_IGN); // Let's ignore SIGPIPE

    struct session *clients = calloc(maxclients,sizeof(struct session));
    int numclients = 0;

    // Create all clients
    int i;
    for (i=0; i<maxclients; i++) {
        struct session *c = &clients[i];
        printf("#%d: Creating a new client\n",i);
        if (client_init(c,bin)!=0) { printf("%d: Client creation failed, aborting\n",i); return -1; }
        numclients++;
    }
    
    int commands_to_run = numtests;
    int running = 1;
    while (running) {
        int maxfd = 0,res;
        fd_set set;
        // Prepare fdset for select()
        FD_ZERO(&set);
        for (i=0; i<maxclients; i++) {
            struct session *c = &clients[i];
            // Add clients that are in state of waiting to read a response
            if ((c->state == cmdsent) || (c->state == inqueue)) {
                FD_SET(c->fdin,&set);
                if (c->fdin > maxfd) maxfd = c->fdin;
            }
        }
        // select
//        printf("Entering select()\n");
        struct timeval timeout = { 1,0 }; // 1 second wait
        res = select(maxfd+1,&set,NULL,NULL,&timeout);
//        printf("select() returned: %d\n",res);
        // check client responses
        for (i=0; i<maxclients; i++) {
            struct session *c = &clients[i];
            // Skip uninitialized clients.
            if ((c->fdin < 0) || (c->state == uninit)) continue;
            if (FD_ISSET(c->fdin, &set)) {
                /* Client is ready for reading, go ahead... */
                res = linebuf_readdata(c->respbuf,c->fdin);
//                res = read(c->fdin, c->response, RESPBUFSIZE-1);
                if (res < 0) { // read failed
                    printf("#%d: Read from client failed\n",i);
                    client_close(c);
                } else if (res == 0) { // pipe closed
                    if (c->state != exiting) {
                        printf("#%d: Client closed connection abruptly!\n",i);
                    }
                    client_close(c);
                } else {
                    char *line;
                    int startnew = 0;
                    while ((line = linebuf_getline(c->respbuf)) != NULL) {
                        printf("#%d: read: %s",i,line);

                        if (c->state == inqueue) {
                            if (strncmp("ready\n",line,6) == 0) {
                                startnew = 1;
                            }
                        } if (c->state == cmdsent) {
                            if ((strncmp("ok:",line,3) == 0) ||
                                (strncmp("fail:",line,5) == 0)) {
                                startnew = 1;
                            }
                        }
                        free(line);
                    }
                    if (startnew == 0) continue;
                    // Are there more commands to run?
                    if (commands_to_run > 0) {
                        // send next command, decrement count
                        if (client_newcmd(c,i) < 0) {
                            client_close(c);
                        } else {
                            commands_to_run--;
                        }
                    } else {  // Otherwise, send quit command
                        if (client_cmdquit(c,i) < 0) {
                            client_close(c);
                        }
                    }
                }
            }
        }

        // reap exited clients
        int ret;
        while ((res = waitpid(-1,&ret,WNOHANG)) > 0) {
            printf("Reaping clients\n");
            client_reap_pid(clients,maxclients,res);
            numclients--;
        }
        // if there are no commands left and all clients are dead, break out
        if ((commands_to_run <= 0) && (numclients == 0)) break;
        // if there are commands left, init one new client
        if ((commands_to_run > 0) && (numclients < maxclients)) {
            // Find empty client index
            for (i=0; i<maxclients; i++) {
                struct session *c = &clients[i];
                if (c->state != uninit) continue;
                if (client_init(c,bin)!=0) { printf("Client initialization failed, aborting\n"); return -1; }
                numclients++;
            }
        }
        printf("cmds: %d, clients: %d (",commands_to_run,numclients);
        for (i=0; i<maxclients; i++) {
            struct session *c = &clients[i];
            switch (c->state) {
            case uninit: printf("u"); break;
            case inqueue: printf("q"); break;
            case cmdsent: printf("s"); break;
            case exiting: printf("x"); break;
            }
        }
        printf(")\n");                    
    }

    printf("Test with %d commands successful\n",numtests);    
    free(clients);
    return 0;
}
