# Compile and Execution Instructions

To compile our project, please run `make` in the project home directory. There are 5 processes that must be running to evaluate all implemented features of our PennCloud project; we include the relevant command line commands to run for each process on the CIS 5050 VM:

  1. **Frontend Server**: `./fserver -v -p [PORT] frontend/config.secret backend/coordinator_server.txt`
  2. **Frontend Load Balancer**: `./flb -v frontend/config.secret`
  3. **SMTP Server**: `./smtp_server -v`
  4. **Backend Coordinator**: `./bcoordinator -v backend/coordinator_server.txt backend/storage_servers.txt`
  4. **Backend Storage Server**: `./btable -v backend/coordinator_server.txt backend/storage_servers.txt [NODE_IDX]`

We include the optional `-v` verbose flag in all commands above. For less verbose outputs to `stdout`, feel free to omit this flag.

Each command line command should be run in a separate shell environment. There are 6 backend storage nodes specified in our [`backend/storage_servers.txt`](backend/storage_servers.txt) configuration file; therefore, the backend storage server command should be run in 6 different terminals, where `[NODE_IDX]` is equal to integer values from 1 to 6 inclusive. There can be any arbitrary number of frontend servers; to run a frontend server binded to a particular integer port value, replace the `[PORT]` variable above with the appropriate port value. The default port value is `2121`.
