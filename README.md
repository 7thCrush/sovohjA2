# Assignment 2:

## How to operate the assignment via make-commands:

- `make` creates the object files needed to run the assignment (testbench, client and server)
- `make launch` launches the server
- `make test` tests the assignment via testbench and client
- `make clean` cleans the object files (testbench, client and server)

## Notes:

Account details are kept in the `account_details.txt` file, and the program logs in the `log.txt` file.
Remember to execute the make-commands in the same directory which has the sockets, and `make launch` and `make test` in different terminals.