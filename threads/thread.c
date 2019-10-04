#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include "stdbool.h"

#define READY 0
#define MURDERED 1




/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
};

/* This is the thread control block */
typedef struct thread {
	Tid id;	//id of the thread
	ucontext_t threadContext; //thread context of the thread
	//int state; //keeps track of the state of thread
	void * sp; //pointer to dynamically allocated stack pointer
	struct thread * nextThread;	//pointer to next thread in queue (linked list)
} Thread;

//function declaration
void thread_stub(void (*thread_main)(void *), void *arg);
//obtains the next possible id for the next thread
Tid get_id();
//inserts a thread into the ready linked list queue
void insertReady(Thread * insert);
//inserts a thread into the murdered linked list queue
void insertMurdered(Thread * insert);
//removes the thread with same id from ready queue linked list and returns a pointer to the one that has been removed. returns null otherwise
Thread * removeReady(Tid id);
//returns a pointer to thread in the murder queue that has the same id
Thread * murderedExists(Tid id);
//prints the ready queue linked list
void printReadyQ();
//incinerate and dispose of murdered threads
void exterminate();

//globals
//pointer to the head of ready linked list
Thread * readyHead = NULL;
//pointer to current running thread
Thread * runningThread = NULL;
//pointer to the head of murdered linked list
Thread * murderedHead = NULL;
//array of ids for threads. makes sure each id is unique
int tidTracker[THREAD_MAX_THREADS];
//flag makes sure get and set context does not loop inside thread_yeild function
volatile int flag = 0;

/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	//Tid ret;

	thread_main(arg); // call thread_main() function with arg
	thread_exit();
}

Tid get_id(){
	for (int i = 0; i < THREAD_MAX_THREADS; i++){
		if (tidTracker[i] == 0){
			return i;
		}
	}
	return THREAD_NOMORE;
}

void insertReady(Thread * insert){
	if (readyHead == NULL){
		readyHead = insert;
		return;
	}
	Thread * ptr = readyHead;
	while (ptr != NULL){
		if (ptr->nextThread == NULL){
			ptr->nextThread = insert;
			return;
		}
		ptr = ptr->nextThread;
	}
}
	
void insertMurdered(Thread * insert){
	if (murderedHead == NULL){
		murderedHead = insert;
		return;
	}
	Thread * ptr = murderedHead;
	while (ptr != NULL){
		if (ptr->nextThread == NULL){
			ptr->nextThread = insert;
			return;
		}
		ptr = ptr->nextThread;
	}
}

Thread * removeReady(Tid id){
	if (readyHead == NULL){
		return NULL;
	}
	Thread * ptr = readyHead;
	Thread * prevPtr = NULL;
	while (ptr != NULL){
		if (ptr->id == id){
			if (ptr == readyHead){
				readyHead = ptr->nextThread;
			}
			else {
				prevPtr->nextThread = ptr->nextThread;
			}
			ptr->nextThread = NULL;
			return ptr;
		}
		prevPtr = ptr;
		ptr = ptr->nextThread;
	}
	return NULL;
}

Thread * murderedExists(Tid id){
	if (murderedHead == NULL){
		return NULL;
	}
	Thread * ptr = murderedHead;
	while (ptr != NULL){
		if (ptr->id == id){
			return ptr;
		}
		ptr = ptr->nextThread;
	}
	return NULL;
}

void printReadyQ(){
	Thread * ptr = readyHead;
	while (ptr != NULL){
		printf("%d -> ", ptr->id);
		if (ptr->nextThread == NULL){
			return;
		}
		
		ptr = ptr->nextThread;
	}
}

void exterminate(){
	Thread * ptr = murderedHead;
	Thread * next = murderedHead;
	while (ptr != NULL){
		next = ptr->nextThread;
		free(ptr->sp);
		ptr->sp = NULL;
		free(ptr);
		ptr = next;
	}
	ptr = NULL;
	next = NULL;
	murderedHead = NULL;
}

void
thread_init(void)
{
	Thread * mainThread = (Thread *)malloc(sizeof(Thread));
	getcontext(&(mainThread->threadContext));

	for(int i=0; i< THREAD_MAX_THREADS; i++){
		tidTracker[i] = 0;
	}

	mainThread->id = get_id();
	tidTracker[mainThread->id] = 1;
	mainThread->nextThread = NULL;
	//mainThread->state = READY;
	mainThread->sp = NULL;
	runningThread = mainThread;
}

Tid
thread_id()
{
	if (runningThread != NULL){
		return runningThread->id;
	}
	return THREAD_INVALID;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	Thread * newThread = (Thread *)malloc(sizeof(Thread));
	if (newThread == NULL){
		free(newThread);
		return THREAD_NOMEMORY;
	}
	if (get_id() == THREAD_NOMORE){
		free(newThread);
		return THREAD_NOMORE;
	}
	newThread->id = get_id();
	tidTracker[newThread->id] = 1;
	newThread->nextThread = NULL;
	//newThread->state = READY;
	newThread->sp = (void *)malloc(THREAD_MIN_STACK);
	if (newThread->sp == NULL){
		free(newThread->sp);
		return THREAD_NOMEMORY;
	}
	getcontext(&(newThread->threadContext));
	newThread->threadContext.uc_stack.ss_size = THREAD_MIN_STACK;
	newThread->threadContext.uc_stack.ss_sp = newThread->sp;
	newThread->threadContext.uc_stack.ss_flags = 0;
	newThread->threadContext.uc_mcontext.gregs[REG_RSP] = (unsigned long) (newThread->sp + THREAD_MIN_STACK - 8);
    newThread->threadContext.uc_mcontext.gregs[REG_RBP] = (unsigned long) newThread->sp;
    newThread->threadContext.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
    newThread->threadContext.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;
	newThread->threadContext.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;

	insertReady(newThread);
	return newThread->id;
}

Tid
thread_yield(Tid want_tid)
{
	if (want_tid == THREAD_ANY) {
		if (readyHead == NULL){
			return THREAD_NONE;
		}
		int runningThreadID;
		getcontext(&runningThread->threadContext);
		if(flag == 0){
			flag = 1;
			insertReady(runningThread);
			Thread * head = removeReady(readyHead->id);
			runningThread = head;
			runningThreadID = runningThread->id;
			setcontext(&head->threadContext);
		}
		flag = 0;
		return runningThreadID;
	}
	else if (want_tid == THREAD_SELF || runningThread->id == want_tid) {
		int runningThreadID;
		getcontext(&runningThread->threadContext);
		if(flag == 0){
			flag = 1;
			runningThreadID = runningThread->id;
			setcontext(&runningThread->threadContext);
		}
		flag = 0;
		return runningThreadID;
	}
	else if (want_tid < THREAD_MAX_THREADS && want_tid >= 0){
		Thread * ptr = murderedExists(want_tid);
		if(ptr != NULL){
			exterminate();
		}
		if (tidTracker[want_tid] == 0){
			return THREAD_INVALID;
		}
		int runningThreadID;
		getcontext(&runningThread->threadContext);
		if(flag == 0){
			flag = 1;
			insertReady(runningThread);
			ptr = removeReady(want_tid);
			runningThread = ptr;
			runningThreadID = runningThread->id;
			setcontext(&runningThread->threadContext);
		}
		flag = 0;
		return runningThreadID;
	}
	else {
		return THREAD_INVALID;
	}
}

void
thread_exit()
{
	if (readyHead == NULL){
		exterminate();
		free (runningThread->sp);
		free(runningThread);
		runningThread = NULL;
		exit(0);
	}
	//runningThread->state = MURDERED;
	tidTracker[runningThread->id] = 0;
	insertMurdered(runningThread);
	Thread * head = removeReady(readyHead->id);
	runningThread = head;
	setcontext(&head->threadContext);
	
}

Tid
thread_kill(Tid tid)
{
	if (tid == runningThread->id || tid >= THREAD_MAX_THREADS || tid < 0 || tidTracker[tid] == 0){
		return THREAD_INVALID;
	}
	Thread * ptr = removeReady(tid);
	if (ptr == NULL){
		return THREAD_INVALID;
	}
	//ptr->state = MURDERED;
	tidTracker[ptr->id] = 0;
	insertMurdered(ptr);
	return ptr->id;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
