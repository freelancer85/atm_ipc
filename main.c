#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "atm.h"

#define SHM_MODE 0666
#define SEM_MODE 0644

union semun
{
        int val;
        struct semid_ds *buf;
        ushort  *array;
        struct seminfo *__buf;
};

static int lock_db(const int semSet){
	struct sembuf sem;
	// ACQUIRE RESOURCE
	sem.sem_num =0;
	sem.sem_flg = SEM_UNDO;
	sem.sem_op = -1;
	if(semop(semSet,&sem,1) == -1){
		perror("semop");
		return -1;
	}
	return 0;
}

static int unlock_db(const int semSet){
	struct sembuf sem;
	// ACQUIRE RESOURCE
	sem.sem_num =0;
	sem.sem_flg = SEM_UNDO;
	sem.sem_op = 1;
	if(semop(semSet, &sem, 1) == -1){
		perror("semop");
		return -1;
	}
	return 0;
}

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

static int get_number(){
	char buf[20];
	int i, len = 0;

	if(get_string(buf, sizeof(buf)) == -1){
		return -1;	//return negative to show its error
	}

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

static void db_Editor(const int msg_q) {

	struct msgbuf mbuf;

	while (1) {

		//clear the account
		bzero(&mbuf.acc, sizeof(struct account));

		// get account number
		printf("Enter account No.: ");
		if(get_string(mbuf.acc.account_no, ACCOUNT_LEN+1) < 0){
			continue;
		}

		//if user wants to quit
		if(strcmp(mbuf.acc.account_no, "X") == 0){
			break;	//stop the loop
		}

		// get pin
		printf("Enter PIN: ");
		mbuf.acc.PIN = get_number();
		if(mbuf.acc.PIN < 0){
			continue;
		}
		//encrypt the pin, before we send it
		mbuf.acc.PIN += 1;

		printf("Enter funds: ");
		mbuf.acc.funds = get_float();
		if(mbuf.acc.funds < 0.0f){
			fprintf(stderr, "Error: Invalid funds amount\n");
			continue;
		}

		// send message to server to update account
		mbuf.mtype = 1;	//to db server
		mbuf.pid = getpid();	//tell server who we are
		mbuf.cmd = CMD_UPDATE_DB;

		// Send through msg queue and wait for reply
		if(	(msgsnd(msg_q, &mbuf, MSG_SIZE, 0) == -1) ||
				(msgrcv(msg_q, &mbuf, MSG_SIZE, getpid(), 0) == -1) ){
			perror("msgsnd/msgrcv");
			break;
		}

		// do the action according to the value in &msgRcv: print balance, withdraw, exit...
		switch(mbuf.cmd){
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
				printf("Balance after update is %.2f\n", mbuf.acc.funds);
				break;

			default:
				fprintf(stderr, "Error: Unknown reply from server\n");
				break;
		}
	}
}

static int load_database(const char * filename, struct account * db, const int db_size){
	int i = 0;
	char line[150];

	FILE * fin = fopen(filename, "r");
	if(fin == NULL){
		perror("fopen");
		return -1;
	}

	while(i < db_size){

		if(fgets(line, sizeof(line), fin) == NULL){
			break;
		}

		const char * id     = strtok(line, ",\r\n");
		const char * pin    = strtok(NULL, ",\r\n");
		const char * amount = strtok(NULL, ",\r\n");

		if((id == NULL) || (pin == NULL) || (amount == NULL)){
			fprintf(stderr, "Error: Invalid record on line %d\n", i);
			break;
		}

		strncpy(db[i].account_no, id, ACCOUNT_LEN);
		db[i].PIN = atoi(pin) - 1;	//PIN is encrypted in db
		db[i].funds = atof(amount);

		i++;
	}

	//if we haven't reached end of file
	if(!feof(fin)){
		return -1;	//return error
	}
	fclose(fin);

	return i;	//return count of db records
}

static int save_database(const char * filename, struct account * db, const int db_size){
	int i = 0;

	FILE * fout = fopen(filename, "w");
	if(fout == NULL){
		perror("fopen");
		return -1;
	}

	for(i=0; i < db_size; i++){
		//encrypt the PIN, by adding 1
		db[i].PIN += 1;

		if(fprintf(fout, "%s,%d,%f\n", db[i].account_no, db[i].PIN, db[i].funds) < 0){
			perror("fprintf");
			db[i].PIN -= 1;
			break;
		}
		//restore original pin in db
		db[i].PIN -= 1;
	}
	fclose(fout);

	return 0;
}

static void db_Interest(struct account *db, const int db_len, const int semSet) {
	int i;

	while (1) {
		//interest updates database every minute
		sleep(60);

		if(lock_db(semSet) < 0){
			break;
		}


		for(i=0; i < db_len; i++){

			if(db[i].funds == 0){	//skip empty accounts
				continue;
			}

			//calculate how much is 1% from funds in account
			const float percent = ((float) db[i].funds / 100.0f);

			if(db[i].funds > 0){
				db[i].funds += percent;		//+ 1%
				//printf("INTEREST: Account %s updated with %.2f interest\n", db[i].account_no, percent);
			}else{
				db[i].funds -= 2.0f * percent;	//- 2%
				//printf("INTEREST: Account %s updated with %.2f interest\n", db[i].account_no, -2.0f * percent);
			}
		}

		if(unlock_db(semSet) < 0){
			break;
		}
	}
}

static void db_Server(const int msg_q, const int semSet, struct account *db, const int db_len) {
	struct msgbuf mbuf;
	int i, retries[DB_SIZE];	//pin retries for each account

	//zero the retries
	bzero(retries, sizeof(int)*DB_SIZE);

	while (1) {

		// The server will constantly wait for messages arriving
		bzero(&mbuf, sizeof(struct msgbuf));
		if(msgrcv(msg_q, &mbuf, MSG_SIZE, 1, 0) == -1){
	 		perror("msgrcv");
	 		break;
		}


		//NOTE: no need to sync, since only we change the account_no ( interest changes only .funds)
		//look for the account
		int ai = -1;
		for(i=0; i < db_len; i++){
			if(strcmp(db[i].account_no, mbuf.acc.account_no) == 0){	//if account no matches
				ai = i;
				break;	//stop looking
			}
		}

		if(ai == -1){	//if account doesn't exist
			mbuf.cmd = CMD_ACC_WRONG;

      //search in blocked accounts
      mbuf.acc.account_no[0] = 'X';
      for(i=0; i < db_len; i++){
  			if(strcmp(db[i].account_no, mbuf.acc.account_no) == 0){	//if account no matches
  				ai = i;
  				break;	//stop looking
  			}
  		}

      mbuf.cmd = (ai == -1) ? CMD_ACC_WRONG : CMD_ACC_BLOCKED;

		}else if(db[ai].account_no[0] == 'X'){	//if account is blocked
			mbuf.cmd = CMD_ACC_BLOCKED;

		// Is it a message to check the PIN?
		}else if(mbuf.cmd == CMD_LOGIN){
			mbuf.cmd = CMD_PIN_WRONG;	//set default error code

			// => check the PIN: , decrypt PIN, compare stored value and the message
			//decrypt PIN, by substracting 1 and compare with db
			if(db[ai].PIN == (mbuf.acc.PIN - 1)){
				// You need to record that the pin was accepted
				retries[ai] = -1;	//clear the retries, since PIN is correct, and mark pin is accepted
				mbuf.cmd = CMD_PIN_OK;
			}else{
				// Fail? Increment the counter of re-attempts; 3 times? block the account
				if(++retries[ai] == 3){
					db[ai].account_no[0] = 'X';	//block the account, for 3 failed logins
				}
				mbuf.cmd = CMD_PIN_WRONG;
			}


		// Is it a message to check the balance?
		}else if(mbuf.cmd == CMD_BALANCE){

			// Was the PIN accepted in a previous step?
			if(retries[ai] == -1){	//if yes

				lock_db(semSet);
				mbuf.acc.funds = db[ai].funds;	// =>  Check the balance
				unlock_db(semSet);

				mbuf.cmd = CMD_BALANCE;
			}else{	//if no
				//return error
				mbuf.cmd = CMD_PIN_WRONG;
			}


		// Is it a message to request for a withdrawal?
		}else if(mbuf.cmd == CMD_WITHDRAW){

			// Was the PIN accepted in a previous step?
			if(retries[ai] == -1){	//if yes
				// =>  Check the balance
				lock_db(semSet);
				if(db[ai].funds >= mbuf.acc.funds){
					// Decrement the balance based on the value received in the message
					db[ai].funds -= mbuf.acc.funds;
					mbuf.acc.funds = db[ai].funds;
					mbuf.cmd = CMD_FUNDS_OK;
				}else{
					mbuf.cmd = CMD_NFS;
				}
				unlock_db(semSet);

			}else{	//if no
				//return error
				mbuf.cmd = CMD_PIN_WRONG;
			}

    // Is it a message to request for a deposit?
    }else if(mbuf.cmd == CMD_DEPOSIT){

			// Was the PIN accepted in a previous step?
			if(retries[ai] == -1){	//if yes
				// =>  Check the balance
				lock_db(semSet);
				// Increment the balance based on the value received in the message
				db[ai].funds += mbuf.acc.funds;
				mbuf.acc.funds = db[ai].funds;
				mbuf.cmd = CMD_DEPOSIT_OK;
				unlock_db(semSet);

			}else{	//if no
				//return error
				mbuf.cmd = CMD_PIN_WRONG;
			}

		// Is it a message asking to update the DB?
		}else if(mbuf.cmd == CMD_UPDATE_DB){

			//check pin before updating account
			mbuf.acc.PIN--;	//decrypt pin
			if(db[ai].PIN == mbuf.acc.PIN){
				// You need to record that the pin was accepted
				retries[ai] = -1;	//clear the retries, since PIN is correct, and mark pin is accepted

				//copy updated info to db
				lock_db(semSet);
				memcpy(&db[i], &mbuf.acc, sizeof(struct account));

				mbuf.cmd = CMD_FUNDS_OK;
				mbuf.acc.funds = db[ai].funds;

				unlock_db(semSet);

				save_database("db.txt", db, db_len);

			}else{
				// Fail? Increment the counter of re-attempts; 3 times? block the account
				if(++retries[ai] == 3){
					db[ai].account_no[0] = 'X';	//block the account, for 3 failed logins
				}
				mbuf.cmd = CMD_PIN_WRONG;
			}

		}else{	//unknown message
			//set error as reply
			mbuf.cmd = CMD_ACC_WRONG;
		}

		//send reply
		mbuf.mtype = mbuf.pid;	//to db server
		mbuf.pid = 1;

		// Send pin through msg queue and wait for reply
		if(msgsnd(msg_q, &mbuf, MSG_SIZE, 0) == -1){
			perror("msgsnd");
			break;
		}
	}
}

int main() {
	int msg_q, shmId;
	key_t key;
	struct account * db;
	union semun sem_init;

	key = ftok(MSG_PATH, MSG_PROJ_ID);
	if(key == -1){
		perror("ftok");
		return -1;
	}

	msg_q = msgget(key, IPC_CREAT | 0666);  // create msg queue
	if(msg_q == -1){
		perror("msgget");
		return -1;
	}

	//create the shared memory for database
	if((shmId = shmget(IPC_PRIVATE, sizeof(struct account)*DB_SIZE, SHM_MODE)) == -1){
		perror("shmget");
		return -1;
	}

	//our semaphore for synchronization of access to database
	const int semSet = semget(IPC_PRIVATE, 1, IPC_CREAT | SEM_MODE);
	if(semSet == -1){
		perror("semget");
		return -1;
	}

	sem_init.val = 1;
	if(semctl(semSet, 0, SETVAL, sem_init) == -1){
		perror("semctl");
		return -1;
	}

	//attach the address to our address space
	db = (struct account *) shmat(shmId, 0, 0);
	if(db == (void*) -1){
		perror("shmat");
		return -1;
	}

	//clear the database
	bzero(db, sizeof(struct account)*DB_SIZE);

	//load the file
	const int db_len = load_database("db.txt", db, DB_SIZE);
	if(db_len < 0){
		return -1;
	}


	//start the editor process
	const pid_t editor_pid = fork();
	if (editor_pid == 0){
		db_Editor(msg_q);

		//tell server we are quiting
		if(kill(getppid(), SIGTERM) == -1){
			perror("kill");
		}
		exit(0);
	}

	//start the interest process
	const pid_t interest_pid = fork();
	if (interest_pid == 0){
		db_Interest(db, db_len, semSet);
		exit(0);
	}


	db_Server(msg_q, semSet, db, db_len);

	//send a signal to editor/interest to exit
	if(	(kill(editor_pid,   SIGTERM) == -1) ||
			(kill(interest_pid, SIGTERM) == -1) ){
		perror("kill");
	}

	//wait for editor to exit
	if(	(waitpid(editor_pid, NULL, 0) == -1) ||
			(waitpid(interest_pid, NULL, 0) == -1) ){
		perror("waitpid");
	}

	msgctl(msg_q, IPC_RMID, NULL); // Delete Message Queue
	shmctl(shmId, IPC_RMID, (struct shmid_ds *) 0);
	semctl(semSet, 0, IPC_RMID, 0);


	return 0;
}
