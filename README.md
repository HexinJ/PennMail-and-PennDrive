# Introduction
By collaborating with three other teammates, I created a small version of Gmail and Google Drive entitled PennMail and PennDrive. PennMail provides an email server that allows users to send emails to other users, view their inbox, and view individual email detail. PennDrive provides distributed file storage that allows users to upload/download files up to 10MB, create nested folders, and rename/move files and folders. 

# Walkthrough and Screenshots

- Create an account
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/signup.png" width="500">

- Login
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/login.png" width="500">

- Home
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/home.png" width="500">

- View Inbox
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/email_list.png" width="500">

- View Email Detail
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/more_email_detail.png" width="500">

- Send Email
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/send_email.png" width="500">

- Files
<img src="https://github.com/HexinJ/PennMail-and-PennDrive/screenshots/files.png" width="500">

# Compile and Execution Instructions

To compile our project, please run `make` in the project home directory. There are 5 processes that must be running to evaluate all implemented features of our PennCloud project; we include the relevant command line commands to run for each process on the CIS 5050 VM:

  1. **Frontend Server**: `./fserver -v -p [PORT] frontend/config.secret backend/coordinator_server.txt`
  2. **Frontend Load Balancer**: `./flb -v frontend/config.secret`
  3. **SMTP Server**: `./smtp_server -v`
  4. **Backend Coordinator**: `./bcoordinator -v backend/coordinator_server.txt backend/storage_servers.txt`
  4. **Backend Storage Server**: `./btable -v backend/coordinator_server.txt backend/storage_servers.txt [NODE_IDX]`

We include the optional `-v` verbose flag in all commands above. For less verbose outputs to `stdout`, feel free to omit this flag.

Each command line command should be run in a separate shell environment. There are 6 backend storage nodes specified in our [`backend/storage_servers.txt`](backend/storage_servers.txt) configuration file; therefore, the backend storage server command should be run in 6 different terminals, where `[NODE_IDX]` is equal to integer values from 1 to 6 inclusive. There can be any arbitrary number of frontend servers; to run a frontend server binded to a particular integer port value, replace the `[PORT]` variable above with the appropriate port value. The default port value is `2121`.
