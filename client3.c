#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>

#define MAX_LENGTH 100 // max length for input and output

void copydata(int from, int to) {
  int amount;
  char buf[1024], resp[100];
  ssize_t r; // returned bytes

  while ((amount = read(from, buf, sizeof(buf))) > 0) { // infinite loop
    assert((write(to, buf, amount) == amount)); // write to socket
    assert((r = read(to, &resp, sizeof(resp))) != -1); // receive from socket (stops the execution)
    resp[r] = '\0'; // null terminate to indicate a string
    printf("%s", resp); // display the info
    fflush(stdout);

    if (strcmp(resp, "ok: Quit the desk\n") == 0) { // if received the quit command, quit reading
        break;
    }
  }
  assert(amount >= 0);
}


int main(void) {
  struct sockaddr_un address;
  int sock, newsock; // sock = main socket, newsock = desk socket
  size_t addrLength;

  assert((sock = socket(PF_UNIX, SOCK_STREAM, 0)) >= 0); // create main socket
  address.sun_family = AF_UNIX; // properties of main address
  strcpy(address.sun_path, "unix_socket");
  addrLength = sizeof(address.sun_family) + strlen(address.sun_path); // address length
  assert((connect(sock, (struct sockaddr *) &address, addrLength)) == 0); // connect to main socket

  char res[MAX_LENGTH];
  assert((read(sock, &res, sizeof(res))) != -1); // read the forwarded socket

  close(sock); // close the prev connection

  assert((newsock = socket(AF_UNIX, SOCK_STREAM, 0)) != -1); // new socket time
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, res); // update the socket path
  addrLength = sizeof(address.sun_family) + strlen(address.sun_path);
  assert((connect(newsock, (struct sockaddr*) &address, addrLength)) != -1); // connect to the desk socket

  int isBank = 0; // send flag
  assert((write(newsock, &isBank, sizeof(int))) != -1);

  char ress[MAX_LENGTH];
  assert((read(newsock, &ress, sizeof(ress))) != -1);
  printf("%s", ress); // our read response includes a newline already
  fflush(stdout);

  copydata(STDIN_FILENO, newsock); // keep sending data until we have received an ack from our quit command

  close(newsock);
  return 0;
}