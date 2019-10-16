#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include "stdbool.h"
 
#define READY 0
#define MURDERED 1
 
/* This is the thread control block */
typedef struct thread {
    Tid id; //id of the thread
    ucontext_t threadContext; //thread context of the thread
    int state; //keeps track of the state of thread
    void * sp; //pointer to dynamically allocated stack pointer
    struct thread * nextThread; //pointer to next thread in queue (linked list)
    struct wait_queue * wq; //pointer to the wait queue
} Thread;
 
/* This is the wait queue structure */
typedef struct wait_queue {
    Thread * waitHead;
    int size;
} WQ;
 
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
//prints the ready queue linked list
void printReadyQ();
//incinerate and dispose of murdered threads
void exterminate();
 
void insertWQ(Thread * insert, WQ * q);
 
//globals
//pointer to the head of ready linked list
Thread * readyHead = NULL;
//pointer to current running thread
Thread * runningThread = NULL;
//pointer to the head of murdered linked list
Thread * murderedHead = NULL;
//array of threads in accordance with their id
Thread * threadTracker[THREAD_MAX_THREADS];
Thread * wq[THREAD_MAX_THREADS];
//flag makes sure get and set context does not loop inside thread_yeild function
volatile int flag = 0;

/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
    interrupts_on();
    //Tid ret;
 
    thread_main(arg); // call thread_main() function with arg
    thread_exit();
    interrupts_on();
}

Tid get_id(){
    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if (threadTracker[i] == NULL){
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
        wait_queue_destroy(ptr->wq);
        ptr->wq = NULL;
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
    getcontext(&mainThread->threadContext);
 
    for(int i=0; i< THREAD_MAX_THREADS; i++){
        threadTracker[i] = NULL;
        wq[i] = NULL;
    }
 
    mainThread->id = get_id();
    threadTracker[mainThread->id] = mainThread;
    mainThread->nextThread = NULL;
    mainThread->state = READY;
    mainThread->sp = NULL;
    mainThread->wq = wait_queue_create();
    runningThread = mainThread;
}

Tid
thread_id()
{
    interrupts_off();
    if (runningThread != NULL){
        interrupts_on();
        return runningThread->id;
    }
    interrupts_on();
    return THREAD_INVALID;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
    interrupts_off();
    Thread * newThread = (Thread *)malloc(sizeof(Thread));
    if (newThread == NULL){
        free(newThread);
        interrupts_on();
        return THREAD_NOMEMORY;
    }
    if (get_id() == THREAD_NOMORE){
        free(newThread);
        interrupts_on();
        return THREAD_NOMORE;
    }
    newThread->id = get_id();
    threadTracker[newThread->id] = newThread;
    newThread->nextThread = NULL;
    newThread->state = READY;
    newThread->wq = wait_queue_create();
    newThread->sp = (void *)malloc(THREAD_MIN_STACK);
    if (newThread->sp == NULL){
        free(newThread->sp);
        interrupts_on();
        return THREAD_NOMEMORY;
    }
    getcontext(&newThread->threadContext);
    newThread->threadContext.uc_stack.ss_size = THREAD_MIN_STACK;
    newThread->threadContext.uc_stack.ss_sp = newThread->sp;
    newThread->threadContext.uc_stack.ss_flags = 0;
    newThread->threadContext.uc_mcontext.gregs[REG_RSP] = (unsigned long) (newThread->sp + THREAD_MIN_STACK - 8);
    newThread->threadContext.uc_mcontext.gregs[REG_RBP] = (unsigned long) newThread->sp;
    newThread->threadContext.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
    newThread->threadContext.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;
    newThread->threadContext.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;
 
    insertReady(newThread);
    interrupts_on();
    return newThread->id;
}

Tid
thread_yield(Tid want_tid)
{
	interrupts_off();
	if (want_tid == THREAD_ANY) {
		if (readyHead == NULL){
			interrupts_on();
			return THREAD_NONE;
		}
		int runningThreadID;
		getcontext(&runningThread->threadContext);
		if(flag == 0){
			flag = 1;
			insertReady(runningThread);
			runningThread = removeReady(readyHead->id);
			runningThreadID = runningThread->id;
			setcontext(&runningThread->threadContext);
		}
		flag = 0;
		interrupts_on();
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
		interrupts_on();
		return runningThreadID;
	}
	else if (want_tid < THREAD_MAX_THREADS && want_tid >= 0){
		Thread * ptr = murderedExists(want_tid);
		if(ptr != NULL){
			exterminate();
		}
		if (threadTracker[want_tid] == NULL){
			interrupts_on();
			return THREAD_INVALID;
		}
		int runningThreadID;
		getcontext(&runningThread->threadContext);
		if(flag == 0){
			flag = 1;
			insertReady(runningThread);
			runningThread = removeReady(want_tid);
			runningThreadID = runningThread->id;
			setcontext(&runningThread->threadContext);
		}
		flag = 0;
		interrupts_on();
		return runningThreadID;
	}
	else {
		interrupts_on();
		return THREAD_INVALID;
	}
}

void
thread_exit()
{
	interrupts_off();
    thread_wakeup(runningThread->wq, 1);
	runningThread->state = MURDERED;
	threadTracker[runningThread->id] = NULL;
	if (readyHead == NULL){
		exterminate();
		free (runningThread->sp);
		wait_queue_destroy(runningThread->wq);
		free(runningThread);
		runningThread = NULL;
		interrupts_on();
		exit(0);
	}
	insertMurdered(runningThread);
	runningThread = removeReady(readyHead->id);
	setcontext(&runningThread->threadContext);	
	interrupts_on();
}    

Tid
thread_kill(Tid tid)
{
    interrupts_off();
    if (tid == runningThread->id || tid >= THREAD_MAX_THREADS || tid < 0 || threadTracker[tid] == NULL){
        interrupts_on();
        return THREAD_INVALID;
    }
    Thread * ptr = removeReady(tid);
    if (ptr == NULL){
        if (threadTracker[tid] != NULL){
            threadTracker[tid]->state = MURDERED;
            interrupts_on();
            return tid;
        }
        interrupts_on();
        return THREAD_INVALID;
    }
    ptr->state = MURDERED;
    threadTracker[ptr->id] = NULL;
	insertMurdered(ptr);
    interrupts_on();
    return ptr->id;
}


/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
void insertWQ(Thread * insert, WQ * queue){
    queue->size += 1;
    if (queue->waitHead == NULL){
        queue->waitHead = insert;
        return;
    }
    Thread * ptr = queue->waitHead;
    while (ptr->nextThread != NULL){
        ptr = ptr->nextThread;
    }
	ptr->nextThread = insert;
}
 
struct wait_queue *
wait_queue_create()
{
    WQ *wq;
 
    wq = malloc(sizeof(WQ));
    assert(wq);
 
    wq->size = 0;
    wq->waitHead = NULL;
 
    return wq;
}
 
void
wait_queue_destroy(struct wait_queue *wq)
{
    if (wq->waitHead == NULL){
        free(wq);
        wq = NULL;
        return;
    }
    Thread * ptr = wq->waitHead;
    Thread * next = wq->waitHead;
    while (ptr != NULL){
        next = ptr->nextThread;
        free(ptr->sp);
        ptr->sp = NULL;
        wait_queue_destroy(ptr->wq);
        ptr->wq = NULL;
        free(ptr);
        ptr = next;
    }
    ptr = NULL;
    next = NULL;
}
 
Tid
thread_sleep(struct wait_queue *queue)
{
    interrupts_off();
    if (queue == NULL){
        interrupts_on();
        return THREAD_INVALID;
    }
    else if (readyHead == NULL){
        interrupts_on();
        return THREAD_NONE;
    }
    else {
        int runningThreadID;
        getcontext(&runningThread->threadContext);
        if (flag == 0){
            flag = 1;
            runningThreadID = runningThread->id;
            insertWQ(runningThread, queue);
            runningThread = removeReady(readyHead->id);
            setcontext(&runningThread->threadContext);
        }
        flag = 0;
        interrupts_on();
        return runningThreadID;
    }
}
 
/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    interrupts_off();
    
    if (queue == NULL){
        interrupts_on();
        return 0;
    }
 
    Thread * head = queue->waitHead;
    if (all == 0){
        queue->waitHead = NULL;
        queue->waitHead = head->nextThread;
        head->nextThread = NULL;
        queue->size -= 1;
        insertReady(head);
        interrupts_on();
        return 1;
    }
    else if (all == 1){
        int size = queue->size;
        Thread * nextPtr = NULL;
        while (head != NULL){
            nextPtr = head->nextThread;
            head->nextThread = NULL;
            insertReady(head);
            head = nextPtr;
        }
        queue->waitHead = NULL;
        queue->size = 0;
        interrupts_on();
        return size;
    }
    interrupts_on();
    return 0;
}
 
/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
    interrupts_off();
    if (tid == runningThread->id || threadTracker[tid] == NULL || tid < 0 || tid >= THREAD_MAX_THREADS){
        interrupts_on();
        return THREAD_INVALID;
    }
    else {
        thread_sleep(threadTracker[tid]->wq);
        exterminate();
        interrupts_on();
        return tid;
    }
}
 
struct lock {
    /* ... Fill this in ... */
    WQ * queue;
    int locked;
};
 
struct lock *
lock_create()
{
    struct lock *lock;
 
    lock = malloc(sizeof(struct lock));
    assert(lock);
 
    lock->queue = wait_queue_create();
    lock->locked = 0;

    return lock;
}
 
void
lock_destroy(struct lock *lock)
{
    assert(lock != NULL);
	wait_queue_destroy(lock->queue);
	free(lock);
}
 
void
lock_acquire(struct lock *lock)
{
    assert(lock != NULL);
    interrupts_off();
	while(lock->locked == 1){
		thread_sleep(lock->queue);
	}
    lock->locked = 1;
    interrupts_on();
}
 
void
lock_release(struct lock *lock)
{
    assert(lock != NULL);
    interrupts_off();
    lock->locked = 0;
	thread_wakeup(lock->queue, 1);
    interrupts_on();
}
 
struct cv {
    /* ... Fill this in ... */
    WQ *  queue;
};
 
struct cv *
cv_create()
{
    struct cv *cv;
 
    cv = malloc(sizeof(struct cv));
    assert(cv);
 
    cv->queue = wait_queue_create();

    return cv;
}
 
void
cv_destroy(struct cv *cv)
{
    assert(cv != NULL);
    wait_queue_destroy(cv->queue);
    free(cv);
}
 
void
cv_wait(struct cv *cv, struct lock *lock)
{
    assert(cv != NULL);
    assert(lock != NULL);
    lock_release(lock);
    thread_sleep(cv->queue);
    lock_acquire(lock);
}
 
void
cv_signal(struct cv *cv, struct lock *lock)
{
    assert(cv != NULL);
    assert(lock != NULL);
    thread_wakeup(cv->queue, 0);
}
 
void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    assert(cv != NULL);
    assert(lock != NULL);
    thread_wakeup(cv->queue, 1);
}
