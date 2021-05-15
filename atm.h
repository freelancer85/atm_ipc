#define ACCOUNT_LEN 5
#define PIN_LEN 3

#define DB_SIZE 100

struct account {
  char account_no[ACCOUNT_LEN + 1];
  int PIN;
  float funds;
};

enum MSG_CMD {
  //ATM -> Server
  CMD_LOGIN=0,
  CMD_BALANCE,
  CMD_WITHDRAW,
  CMD_DEPOSIT,

  CMD_UPDATE_DB,  //Editor -> server

  CMD_PIN_OK,     //valid PIN
  CMD_PIN_WRONG,  //invalid PIN

  CMD_ACC_WRONG,    //no such account
  CMD_ACC_BLOCKED,  //account is blocked

  CMD_FUNDS_OK,     //withdraw success
  CMD_DEPOSIT_OK,   //deposit success
  CMD_NFS           //no sufficient funds
};

struct msgbuf {
  long mtype;       /* message type, must be > 0 */
  pid_t pid;        //sender pid, used to return the message
  enum MSG_CMD cmd;
  struct account acc;
};

//our message queue details
#define MSG_PATH "atm.h"
#define MSG_PROJ_ID 4333

//size of the message
#define MSG_SIZE sizeof(struct msgbuf) - sizeof(long)
