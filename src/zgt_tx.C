/***************** Transaction class **********************/
/*** Implements methods that handle Begin, Read, Write, ***/
/*** Abort, Commit operations of transactions. These    ***/
/*** methods are passed as parameters to threads        ***/
/*** spawned by Transaction manager class.              ***/
/**********************************************************/

/* Required header files */
#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include "zgt_def.h"
#include "zgt_tm.h"
#include "zgt_extern.h"
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <pthread.h>


extern void *start_operation(long, long);  //starts opeartion by doing conditional wait
extern void *finish_operation(long);       // finishes abn operation by removing conditional wait
extern void *open_logfile_for_append();    //opens log file for writing
extern void *do_commit_abort(long, char);   //commit/abort based on char value (the code is same for us)

extern zgt_tm *ZGT_Sh;			// Transaction manager object

FILE *logfile; //declare globally to be used by all

/* Transaction class constructor */
/* Initializes transaction id and status and thread id */
/* Input: Transaction id, status, thread id */

zgt_tx::zgt_tx( long tid, char Txstatus,char type, pthread_t thrid){
  this->lockmode = (char)' ';  //default
  this->Txtype = type; //Fall 2014[jay] R = read only, W=Read/Write
  this->sgno =1;
  this->tid = tid;
  this->obno = -1; //set it to a invalid value
  this->status = Txstatus;
  this->pid = thrid;
  this->head = NULL;
  this->nextr = NULL;
  this->semno = -1; //init to  an invalid sem value
}

/* Method used to obtain reference to a transaction node      */
/* Inputs the transaction id. Makes a linear scan over the    */
/* linked list of transaction nodes and returns the reference */
/* of the required node if found. Otherwise returns NULL      */

zgt_tx* get_tx(long tid1){  
  zgt_tx *txptr, *lastr1;
  
  if(ZGT_Sh->lastr != NULL){	// If the list is not empty
      lastr1 = ZGT_Sh->lastr;	// Initialize lastr1 to first node's ptr
      for  (txptr = lastr1; (txptr != NULL); txptr = txptr->nextr)
	    if (txptr->tid == tid1) 		// if required id is found									
	       return txptr; 
      return (NULL);			// if not found in list return NULL
   }
  return(NULL);				// if list is empty return NULL
}

/* Method that handles "BeginTx tid" in test file     */
/* Inputs a pointer to transaction id, obj pair as a struct. Creates a new  */
/* transaction node, initializes its data members and */
/* adds it to transaction list */

void *begintx(void *arg){
  //Initialize a transaction object. Make sure it is
  //done after acquiring the semaphore for the tm and making sure that 
  //the operation can proceed using the condition variable. when creating
  //the tx object, set the tx to TR_ACTIVE and obno to -1; there is no 
  //semno as yet as none is waiting on this tx.
  
    struct param *node = (struct param*)arg;// get tid and count
    start_operation(node->tid, node->count); 
    zgt_tx *tx = new zgt_tx(node->tid,TR_ACTIVE, node->Txtype, pthread_self());	// Create new tx node
    open_logfile_for_append();
    fprintf(logfile, "T%d\t\t%c \t\tBeginTx\n", node->tid, node->Txtype);	// Write log record and close
    fflush(logfile);
  
    zgt_p(0);				// Lock Tx manager; Add node to transaction list
  
    tx->nextr = ZGT_Sh->lastr;
    ZGT_Sh->lastr = tx;   
    zgt_v(0); 			// Release tx manager 
  
  finish_operation(node->tid);
  pthread_exit(NULL);				// thread exit

}

/* Method to handle Readtx action in test file    */
/* Inputs a pointer to structure that contans     */
/* tx id and object no to read. Reads the object  */
/* if the object is not yet present in hash table */
/* or same tx holds a lock on it. Otherwise waits */
/* until the lock is released */

void *readtx(void *arg){
	//do the operations for reading. Write your code
	struct param *node = (struct param*)arg;						// get tid and objno and count
	start_operation(node->tid, node->count);
	zgt_p(0);       												// Lock Tx manager
	zgt_tx *tx = get_tx(node->tid);									//Retrieving current transaction
	char state = tx->get_status(); 									// fetching current transaction state
	if(tx != NULL){
		if(state == 'P'){											// Transaction in active state
			zgt_v(0);       										// Release tx manager														
			if(tx->set_lock(node->tid, tx-> sgno, node->obno, node->count, 'S') == 1);	//acquired shared mode lock to read. 
				tx->perform_readWrite(tx-> tid, node->obno, 'S');	//read write operation
		}
	}else{															//Transaction doesn't exist
		zgt_v(0);       											// Release tx manager
		fprintf(logfile, "\t Transaction not found. \n", node->tid); 	// Write log file
		fflush(logfile);
	}
	finish_operation(node->tid);
	pthread_exit(NULL);
}


void *writetx(void *arg){ 												//do the operations for writing; similar to readTx
	//do the operations for writing; similar to readTx. Write your code
	struct param *node = (struct param*)arg;							// struct parameter that contains
	start_operation(node->tid, node->count);
	zgt_p(0);       													// Lock Tx manager
	zgt_tx *tx = get_tx(node->tid);										//Retrieving current transaction
	char state = tx->get_status(); 										// fetching current transaction state
	if(tx != NULL){
		if(state == 'P'){
			zgt_v(0);       											// Release tx manager

        //Check if lock can be obtained by current Txn
			if(tx->set_lock(node->tid, tx-> sgno, node->obno, node->count, 'X') == 1);		//acquired exclusive mode lock to write. 
				tx->perform_readWrite(tx-> tid, node->obno, 'X');							// write operation. Exclusive Mode write
		}
	}else{																//Transaction doesn't exist
		zgt_v(0);       												// Release tx manager
		fprintf(logfile, "\t Transaction not found. \n", node->tid); 	// Write log file
		fflush(logfile);
	}
	finish_operation(node->tid);
	pthread_exit(NULL);
}

void *aborttx(void *arg)
{
	//write your code
	struct param *node = (struct param*)arg;							// get tid and count  
	start_operation(node->tid, node->count);
	zgt_p(0);       													// Lock Tx manager
	zgt_tx *tx = get_tx(node->tid);
	if(tx != NULL){
		tx->status = 'A';  												//Change status to Abort
		do_commit_abort(node->tid,'A');									// Setting the status of the transaction to "Abort"
		fprintf(logfile, "T%d\t\t  \t\tAbortTx \t \n", node->tid);
	}else{																//Transaction doesn't exist
		fprintf(logfile, "\t Transaction not found. \n", node->tid);
	}
	fflush(logfile);
	zgt_v(0);       													// Release tx manager
	finish_operation(node->tid);
	pthread_exit(NULL);													// thread exit
}

void *committx(void *arg)
{
	//write your code
	struct param *node = (struct param*)arg;							// get tid and count  
	start_operation(node->tid, node->count);
	zgt_p(0);       													// Lock Tx manager
	zgt_tx *tx = get_tx(node->tid);
	if(tx != NULL){
		tx->status = 'E'; 												//Change status to END
		do_commit_abort(node->tid,'E');									// Setting the status of the transaction to "End"
		fprintf(logfile, "T%d\t\t  \t\tCommitTx \t \n", node->tid);
	}else{																//Transaction doesn't exist
		fprintf(logfile, "\t Transaction not found. \n", node->tid);
	}
	fflush(logfile);
	zgt_v(0);       													// Release tx manager
	finish_operation(node->tid);
	pthread_exit(NULL);													// thread exit
}

// called from commit/abort with appropriate parameter to do the actual
// operation. Make sure you give error messages if you are trying to
// commit/abort a non-existant tx

void *do_commit_abort(long t, char status){
	// write your code
	zgt_tx *tx=get_tx(t);												// Fetch current txn - abort/commit
	if(tx != NULL){
		tx->free_locks();												// Releasing all the locks held by this transaction
		zgt_v(tx->tid);													// remove txn from transaction manager
		int currTxnSemNo = tx->semno;
		tx->end_tx();
		if(currTxnSemNo > -1){
			int totalTxnWaiting = zgt_nwait(currTxnSemNo); 				//Get the # of Txn's waiting on a given semaphore; This is needed to release
																		// all Txs that are waiting on a tx when it commits or aborts.
			if(totalTxnWaiting > 0){
				for (int i = 0; i < totalTxnWaiting; ++i){   
					zgt_v(currTxnSemNo);   			 					//Release all semaphores of waiting txns
				}
			}
		}
	}else{
		fprintf(logfile, "\t Transaction not found. \n", t);
		fflush(logfile);
	}
}

int zgt_tx::remove_tx ()
{
  //remove the transaction from the TM
  
  zgt_tx *txptr, *lastr1;
  lastr1 = ZGT_Sh->lastr;
  for(txptr = ZGT_Sh->lastr; txptr != NULL; txptr = txptr->nextr){	// scan through list
	  if (txptr->tid == this->tid){		// if req node is found          
		 lastr1->nextr = txptr->nextr;	// update nextr value; done
		 //delete this;
         return(0);
	  }
	  else lastr1 = txptr->nextr;			// else update prev value
   }
  fprintf(logfile, "Trying to Remove a Tx:%d that does not exist\n", this->tid);
  fflush(logfile);
  printf("Trying to Remove a Tx:%d that does not exist\n", this->tid);
  fflush(stdout);
  return(-1);
}

/* this method sets lock on objno1 with lockmode1 for a tx in this*/

int zgt_tx::set_lock(long tid1, long sgno1, long obno1, int count, char lockmode1){
  //if the thread has to wait, block the thread on a semaphore from the
  //sempool in the transaction manager. Set the appropriate parameters in the
  //transaction list if waiting.
  //if successful  return(0);
  //write your code
	zgt_hlink *htObj = ZGT_Ht->find(sgno, obno);			//finds the object in the hash table; returns NULL if it is not there
	zgt_p(0);
	zgt_tx *tx = get_tx(tid1);

	if(htObj == NULL){									//Object is not in Hash table
		ZGT_Ht->add(tx, tx->sgno, obno1, tx->lockmode);	//add object in hash table and grant the lock
		tx->status = 'P';  							
		zgt_v(0);       								// Release tx manager
		return(1);										// return to perform read-write
	}else{												//Current transaction already has the lock and is trying to access the object
		if(htObj->tid == tid1){
			tx->status = 'P';
			zgt_v(0);
			return(1);									// return to perform read-write
		}else{
			zgt_tx *secTxn = get_tx(htObj->tid);  		//Txn holding the object
			zgt_hlink *sec1Txn = others_lock(htObj, sgno1, obno1);
			if(tx->Txtype=='R' && secTxn->Txtype=='R' && sec1Txn->lockmode!='X'){ //Check current and secondary txn's type and mode
				if(tx->head == NULL){
					ZGT_Ht->add(tx, tx->sgno, obno1, tx->lockmode);		// add current txn in hash table.
					htObj=ZGT_Ht->find(tx-> sgno, obno1);
					tx->head = htObj;
					tx->status = 'P';
					zgt_v(0);
					return(1);							// return to perform read-write
				}else{
					zgt_hlink *headp = tx->head;		//loop to reach at end and add the object
					while(headp->nextp != NULL){
						headp = headp->nextp;
					}
					headp->nextp = htObj;               // change the head pointer to current txn object
					tx->status = 'P';
					zgt_v(0);
					return(1);							// return to perform read-write
				}
			}	
			else{										//  if not in shared mode
				tx->status = 'W';         				//not in shared, change the status to wait
				tx->obno = obno1;
				tx->lockmode = lockmode1;
				if(get_tx(htObj->tid))
					tx->setTx_semno(htObj->tid, htObj->tid);	//set semaphore 
				else{
					tx->status = 'P';
					zgt_v(0);
					return(1);							// return to perform read-write
				}
				printf("Txn %d is waiting :T%d\n", tx->tid, htObj->tid);
				tx->print_tm();
				zgt_v(0);
				zgt_p(htObj->tid); 						//lock object on transaction manager
				return(0);
			}
		}
	}
}

// this part frees all locks owned by the transaction
// Need to free the thread in the waiting queue
// try to obtain the lock for the freed threads
// if the process itself is blocked, clear the wait and semaphores

int zgt_tx::free_locks()
{
  zgt_hlink* temp = head;  //first obj of tx
  
  open_logfile_for_append();
   
  for(temp;temp != NULL;temp = temp->nextp){	// SCAN Tx obj list

      fprintf(logfile, "%d : %d \t", temp->obno, ZGT_Sh->objarray[temp->obno]->value);
      fflush(logfile);
      
      if (ZGT_Ht->remove(this,1,(long)temp->obno) == 1){
	   printf(":::ERROR:node with tid:%d and onjno:%d was not found for deleting", this->tid, temp->obno);		// Release from hash table
	   fflush(stdout);
      }
      else {
#ifdef TX_DEBUG
	   printf("\n:::Hash node with Tid:%d, obno:%d lockmode:%c removed\n",
                            temp->tid, temp->obno, temp->lockmode);
	   fflush(stdout);
#endif
      }
    }
  fprintf(logfile, "\n");
  fflush(logfile);
  
  return(0);
}		

// CURRENTLY Not USED
// USED to COMMIT
// remove the transaction and free all associate dobjects. For the time being
// this can be used for commit of the transaction.

int zgt_tx::end_tx()  //2014: not used
{
  zgt_tx *linktx, *prevp;

  linktx = prevp = ZGT_Sh->lastr;
  
  while (linktx){
    if (linktx->tid  == this->tid) break;
    prevp  = linktx;
    linktx = linktx->nextr;
  }
  if (linktx == NULL) {
    printf("\ncannot remove a Tx node; error\n");
    fflush(stdout);
    return (1);
  }
  if (linktx == ZGT_Sh->lastr) ZGT_Sh->lastr = linktx->nextr;
  else {
    prevp = ZGT_Sh->lastr;
    while (prevp->nextr != linktx) prevp = prevp->nextr;
    prevp->nextr = linktx->nextr;    
  }
}

// currently not used
int zgt_tx::cleanup()
{
  return(0);
  
}

// check which other transaction has the lock on the same obno
// returns the hash node
zgt_hlink *zgt_tx::others_lock(zgt_hlink *hnodep, long sgno1, long obno1)
{
  zgt_hlink *ep;
  ep=ZGT_Ht->find(sgno1,obno1);
  while (ep)				// while ep is not null
    {
      if ((ep->obno == obno1)&&(ep->sgno ==sgno1)&&(ep->tid !=this->tid)) 
	return (ep);			// return the hashnode that holds the lock
      else  ep = ep->next;		
    }					
  return (NULL);			//  Return null otherwise 
  
}

// routine to print the tx list
// TX_DEBUG should be defined in the Makefile to print

void zgt_tx::print_tm(){
  
  zgt_tx *txptr;
  
#ifdef TX_DEBUG
  printf("printing the tx list \n");
  printf("Tid\tTxType\tThrid\t\tobjno\tlock\tstatus\tsemno\n");
  fflush(stdout);
#endif
  txptr=ZGT_Sh->lastr;
  while (txptr != NULL) {
#ifdef TX_DEBUG
    printf("%d\t%c\t%d\t%d\t%c\t%c\t%d\n", txptr->tid, txptr->Txtype, txptr->pid, txptr->obno, txptr->lockmode, txptr->status, txptr->semno);
    fflush(stdout);
#endif
    txptr = txptr->nextr;
  }
  fflush(stdout);
}

//currently not used
void zgt_tx::print_wait(){

  //route for printing for debugging
  
  printf("\n    SGNO        TxType       OBNO        TID        PID         SEMNO   L\n");
  printf("\n");
}
void zgt_tx::print_lock(){
  //routine for printing for debugging
  
  printf("\n    SGNO        OBNO        TID        PID   L\n");
  printf("\n");
  
}

// routine to perform the acutual read/write operation
// based  on the lockmode
void zgt_tx::perform_readWrite(long tid,long obno, char lockmode){
  // write your code
	//zgt_p(0);        // Lock Tx manager;
	zgt_tx *tx=get_tx(tid);
	int itemValue = ZGT_Sh->objarray[obno]->value;
	if(lockmode == 'X'){   									//exclusive mode, write operation
		ZGT_Sh->objarray[obno]->value = itemValue + 1; 		// for write, update value of obj, incrementing value
		fprintf(logfile, "T%d\t     \t\tWriteTx\t\t%d:%d:%d  \t\t\tWriteLock\tGranted\t\t%c\n",
				tid,obno,ZGT_Sh->objarray[obno]->value,ZGT_Sh->optime[tid],tx->status);  // Write to log file
		fflush(logfile);
	}else{													//shared mode, read operation
		ZGT_Sh->objarray[obno]->value = itemValue - 1; 		// for read, update value of obj, decrementing value
		fprintf(logfile, "T%d\t     \t\tReadTx\t\t%d:%d:%d  \t\t\tReadLock\tGranted\t\t%c\n",
				tid,obno,ZGT_Sh->objarray[obno]->value,ZGT_Sh->optime[tid],tx->status);  // Write to log file
		fflush(logfile);
    //zgt_v(0);       // Release tx manager
	}
}

// routine that sets the semno in the Tx when another tx waits on it.
// the same number is the same as the tx number on which a Tx is waiting
int zgt_tx::setTx_semno(long tid, int semno){
  zgt_tx *txptr;
  
  txptr = get_tx(tid);
  if (txptr == NULL){
    printf("\n:::ERROR:Txid %d wants to wait on sem:%d of tid:%d which does not exist\n", this->tid, semno, tid);
    fflush(stdout);
    return(-1);
  }
  if (txptr->semno == -1){
    txptr->semno = semno;
    return(0);
  }
  else if (txptr->semno != semno){
#ifdef TX_DEBUG
    printf(":::ERROR Trying to wait on sem:%d, but on Tx:%d\n", semno, txptr->tid);
    fflush(stdout);
#endif
    exit(1);
  }
  return(0);
}

// routine to start an operation by checking the previous operation of the same
// tx has completed; otherwise, it will do a conditional wait until the
// current thread signals a broadcast

void *start_operation(long tid, long count){
  
  pthread_mutex_lock(&ZGT_Sh->mutexpool[tid]);	// Lock mutex[t] to make other
  // threads of same transaction to wait
  
  while(ZGT_Sh->condset[tid] != count)		// wait if condset[t] is != count
    pthread_cond_wait(&ZGT_Sh->condpool[tid],&ZGT_Sh->mutexpool[tid]);
  
}

// Otherside of the start operation;
// signals the conditional broadcast

void *finish_operation(long tid){
  ZGT_Sh->condset[tid]--;	// decr condset[tid] for allowing the next op
  pthread_cond_broadcast(&ZGT_Sh->condpool[tid]);// other waiting threads of same tx
  pthread_mutex_unlock(&ZGT_Sh->mutexpool[tid]); 
}

void *open_logfile_for_append(){
  
  if ((logfile = fopen(ZGT_Sh->logfile, "a")) == NULL){
    printf("\nCannot open log file for append:%s\n", ZGT_Sh->logfile);
    fflush(stdout);
    exit(1);
  }
}
