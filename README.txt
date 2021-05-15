To compile just type make in the source directory.
  $ make
gcc -Wall -Wextra -g -o atm atm.c
gcc -Wall -Wextra -g -o main main.c

For running the program, you will need at least 2 terminals - one for atm and one for server ( see sreen.png)

The design of program is the following:
  Server, Editor and Interest processes are in a single file - main.c
  The parent process of those 3 processes, creates all of the shared resources we use:

    a) shared memory for holding the database
      Database is loaded at main process creation, from db.txt file,
      and is stored in shared memory, which is acccessible by Server and Interest processes.
      When Editor sends a command for record update, server updates memory database and the file on disk.

    b) semaphore for locking access to the database
      It ise used by the Server and Interest process, to synchronize access
      to the funds of accounts in database.

    c) message queue for exchanging message between Server and Editor/ATM processes
      Each message consist of the command id + account record. Commands are described in PDF.

      For message delivery we use the PIDs of process as message type. When we send from ATM to server,
    we set ATM PID as sender, and 1 as receiver. Messages with type=1 are always for server.
      When server gets the message, he replies using the senders PID, as a type. This way, each process,
    listens for messages of his own type.


For part f:
  Deadlocks can't occur, because the critical sections, when we update the database,
are short and have no user intervention. Only Server and Interest process access the shared database,
and all they do is update the interest of a few accounts.


For extra:
  We have added the option to deposit an amount of money, at the ATM. The deposit happens,
after the user has entered an account and PIN number is verified. This way user is sure,
he is depositing at his own account. After the success of operation, user sees his new balance.
