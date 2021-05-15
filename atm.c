#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#include "atm.h"

static int get_string(char * buf, const size_t buf_size){
	int len;

	if(fgets(buf, buf_size, stdin) == NULL){
		perror("fgets");
		return -1;
	}
	len = strlen(buf);

	//if we didn't read the whole input, read until newline
	if(buf[len-1] != '\n'){
		//skip extra characters
		while(fgetc(stdin) != '\n'){
			if(feof(stdin) || (ferror(stdin))){
				break;
			}
		}
	}


	//remove newlines
	if(buf[len-1] == '\n'){
		buf[--len] = '\0';
	}

	if(buf[len-1] == '\r'){
		buf[--len] = '\0';
	}

	//terminate string
	buf[len] = '\0';

	return 0;
}

//Get a number from user
static int get_number(){
	char buf[20];
	int i, len = 0;

	//get input from user
	if(get_string(buf, sizeof(buf)) == -1){
		return -1;	//return negative to show its error
	}

	//validate it
	len = strlen(buf);
	//valide the input is a number
	for(i=0; i < len; i++){
		if(!isdigit(buf[i])){
			printf("Error: Invalid number\n");
			return -1;	//return negative to show its error
		}
	}

	return atoi(buf);
}

//get menu id from user
static int get_menu_id(){
	int id=-1;

	while(1){
		//show the ATM menu
		printf("ATM Menu\n");
		printf("1. Balance\n");
		printf("2. Withdraw\n");
		printf("3. Deposit\n");
		printf("4. Quit\n");

		printf("Enter menu id: ");
		//check input value is correct
		id = get_number();
		if((id <= 0) || (id > 4)){
			printf("Error: Invalid menu id\n");
			continue;
		}

		//id is valid
		break;	//stop the loop
	}

	if(id == 4){	//if user wants to quit
		id = -1;
	}

	return id;
}

static int get_float(){
	char buf[20];
	int i, len = 0;

	if(get_string(buf, sizeof(buf)) == -1){
		return -1.0f;	//return negative to show its error
	}

	len = strlen(buf);
	//valide the input is a number
	for(i=0; i < len; i++){
		if(!isdigit(buf[i]) && (buf[i] != '.') ){
			printf("Error: Invalid amount\n");
			return -1.0f;	//return negative to show its error
		}
	}

	return atof(buf);
}

int main() {
	int msg;
	key_t key;
	int pin_retry = 0;

	key = ftok(MSG_PATH, MSG_PROJ_ID);
	if(key == -1){
		perror("ftok");
		return -1;
	}

	msg = msgget(key, IPC_CREAT | 0666);  // create msg queue
	if(msg == -1){
		perror("msgget");
		return -1;
	}

	while (1) {    // loop forever
		struct account acc;
		struct msgbuf mbuf;
		int menu_id = 0;

		//clear the account
		bzero(&acc, sizeof(struct account));

		// get account number, get pin (from the user)
		printf("Enter account No.: ");
		if(get_string(acc.account_no, ACCOUNT_LEN + 1) < 0){
			break;
		}

		//if user wants to quit
		if(strcmp(acc.account_no, "X") == 0){
			break;	//stop the loop
		}

		printf("Enter PIN: ");
		acc.PIN = get_number();
		if(acc.PIN < 0){
			break;
		}

		//encrypt the pin, before we send it
		acc.PIN += 1;

		// Check your pin: to do that, send a message to the db_server and get result
		mbuf.mtype = 1;	//to db server
		mbuf.pid = getpid();	//tell server who we are
		mbuf.cmd = CMD_LOGIN;
		memcpy(&mbuf.acc, &acc, sizeof(struct account));

		// Send pin through msg queue and wait for reply
		if(	(msgsnd(msg, &mbuf, MSG_SIZE, 0) == -1) ||
				(msgrcv(msg, &mbuf, MSG_SIZE, getpid(), 0) == -1) ){
			perror("msgsnd/msgrcv");
			break;
		}

		// Check value of msgRcv and decide what to do: PIN OK, re-attempt, block the account
		if(mbuf.cmd == CMD_PIN_WRONG){
			if(++pin_retry == 3){
				printf("account is blocked\n");
				break;
			}
			printf("Error: Invalid PIN or account No.\n");
			continue;	//re-ask user for PIN
		}else if(mbuf.cmd == CMD_ACC_WRONG){
			printf("Error: Invalid account No.\n");
			continue;
		}else if(mbuf.cmd == CMD_ACC_BLOCKED){
			printf("account is blocked\n");
			continue;
		}


		// Ask for the operation requested: withdraw, check balance, etc.
		menu_id = get_menu_id();
		if(menu_id == -1){//if user wants to quit or error
			break;
		}

		mbuf.mtype = 1;	//to db server
		mbuf.pid = getpid();	//tell server who we are
		memcpy(&mbuf.acc, &acc, sizeof(struct account));
		mbuf.acc.funds = 0.0f;

		// according to the selection by the user:
		switch(menu_id){
			case 1:
				mbuf.cmd = CMD_BALANCE;
				break;

			case 2:
				mbuf.cmd = CMD_WITHDRAW;

				printf("Enter amount to withdraw: ");
				mbuf.acc.funds = get_float();
				break;

			case 3:
				mbuf.cmd = CMD_DEPOSIT;
				printf("Enter amount to deposit: ");
				mbuf.acc.funds = get_float();
				break;

			default:
				break;
		}

		//if user entered invalid funds
		if(mbuf.acc.funds < 0.0f){
			fprintf(stderr, "Error: Invalid amount\n");
			continue;
		}

		//send user request and wait for reply
		if(	(msgsnd(msg, &mbuf, MSG_SIZE, 0) == -1) ||
				(msgrcv(msg, &mbuf, MSG_SIZE, getpid(), 0) == -1) ){
			perror("msgsnd/msgrcv");
			break;
		}

		// do the action according to the value in &msgRcv: print balance, withdraw, exit...
		switch(mbuf.cmd){
			case CMD_BALANCE:
				printf("Balance is %.2f\n", mbuf.acc.funds);
				break;

			case CMD_PIN_OK:     //valid PIN
				printf("Login successfull\n");
				break;

			case CMD_PIN_WRONG:  //invalid PIN
				printf("Login failed\n");
				break;

			case CMD_ACC_WRONG:    //no such account
				printf("No such account\n");
				break;

			case CMD_ACC_BLOCKED:  //account is blocked
				printf("Account is blocked\n");
				break;

			case CMD_FUNDS_OK:     //withdraw success
				printf("Balance after withdrawal is %.2f\n", mbuf.acc.funds);
				break;

			case CMD_NFS:           //no sufficient funds
				printf("No sufficient funds\n");
				break;

			case CMD_DEPOSIT_OK:     //withdraw success
				printf("Balance after deposit is %.2f\n", mbuf.acc.funds);
				break;

			default:
				fprintf(stderr, "Error: Unknown reply from server\n");
				break;
		}
	}
	printf("Bye\n");

	return 0;
}
