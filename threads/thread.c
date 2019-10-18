#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include "stdbool.h"
 
#define READY 0
#define MURDERED 1
#define ASLEEP 2
 
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
//inserts a thread into the corresponding wait queue
void insertWQ(Thread * insert, WQ * q);
//prints a wait queue;
void printWQ(WQ * queue);
 
//globals
//pointer to the head of ready linked list
Thread * readyHead = NULL;
//pointer to current running thread
Thread * runningThread = NULL;
//pointer to the head of murdered linked list
Thread * murderedHead = NULL;
//array of threads in accordance with their id
Thread * threadTracker[THREAD_MAX_THREADS];
//array of wait queues according to their thread id
WQ wq[THREAD_MAX_THREADS];
//flag makes sure get and set context does not loop inside thread_yeild function
int flag = 0;

/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
    interrupts_on();
    //Tid ret;
 
    thread_main(arg); // call thread_main() function with arg
    thread_exit();
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

void printReadyQ(){
	Thread * ptr = readyHead;
    printf("rq:\n");
	while (ptr != NULL){
		printf("%d -> ", ptr->id);
		ptr = ptr->nextThread;
	}
    printf("\n");
}

void exterminate(){
    Thread * ptr = murderedHead;
    Thread * next;
    while (ptr != NULL){
        next = ptr->nextThread;
        
        threadTracker[ptr->id] = NULL;
        free(ptr->sp);
        ptr->sp = NULL;
        ptr->wq->waitHead = NULL;
        ptr->wq->size = 0;
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
    }
 
    mainThread->id = 0;
    threadTracker[0] = mainThread;
    mainThread->nextThread = NULL;
    mainThread->state = READY;
    mainThread->sp = NULL;
    mainThread->wq = &wq[0];
    mainThread->wq->size = 0;
    mainThread->wq->waitHead = NULL;
    runningThread = mainThread;
}

Tid
thread_id()
{
    int enabled = interrupts_off();
    if (runningThread != NULL){
        interrupts_set(enabled);
        return runningThread->id;
    }
    interrupts_set(enabled);
    return THREAD_INVALID;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
    int enabled = interrupts_off();
    Thread * newThread = (Thread *)malloc(sizeof(Thread));
    if (newThread == NULL){
        free(newThread);
        interrupts_set(enabled);
        return THREAD_NOMEMORY;
    }
    if (get_id() == THREAD_NOMORE){
        free(newThread);
        interrupts_set(enabled);
        return THREAD_NOMORE;
    }
    newThread->id = get_id();
    threadTracker[newThread->id] = newThread;
    newThread->nextThread = NULL;
    newThread->state = READY;
    newThread->wq = &wq[newThread->id];
    newThread->wq->waitHead = NULL;
    newThread->wq->size = 0;
    newThread->sp = (void *)malloc(THREAD_MIN_STACK);
    if (newThread->sp == NULL){
        free(newThread->sp);
        interrupts_set(enabled);
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
    interrupts_set(enabled);
    return newThread->id;
}

Tid
thread_yield(Tid want_tid)
{
	int enabled = interrupts_off();
    exterminate();
	if (want_tid == THREAD_ANY) {
		if (readyHead == NULL){
			interrupts_set(enabled);
			return THREAD_NONE;
		}        
        Thread * head = removeReady(readyHead->id);
        int runningThreadID = head->id;
        insertReady(runningThread);
        flag = 0;
        getcontext(&runningThread->threadContext);
        if (runningThread->state == MURDERED){
            thread_exit();
        }
        if (flag == 0){
            flag = 1;
            runningThread = head;
            setcontext(&head->threadContext);
        }
        interrupts_set(enabled);
        return runningThreadID;
	}
	else if (want_tid == THREAD_SELF || runningThread->id == want_tid) {
		getcontext(&runningThread->threadContext);
		if(flag == 0){
			flag = 1;
			setcontext(&runningThread->threadContext);
		}
		interrupts_set(enabled);
		return runningThread->id;
	}
	else if (want_tid < THREAD_MAX_THREADS && want_tid >= 0){
		if (threadTracker[want_tid] == NULL){
			interrupts_set(enabled);
			return THREAD_INVALID;
		}
        Thread * foundThread = removeReady(want_tid);
        int runningThreadID = foundThread->id;
        insertReady(runningThread);
        flag = 0;
        getcontext(&runningThread->threadContext);
        if (runningThread->state == MURDERED){
            thread_exit();
        }
        if (flag == 0){
            flag = 1;
            runningThread = foundThread;
            setcontext(&foundThread->threadContext);
        }
        interrupts_set(enabled);
        return runningThreadID;

	}
    interrupts_set(enabled);
    return THREAD_INVALID;
}

void
thread_exit()
{
	int enabled = interrupts_off();
    thread_wakeup(&wq[runningThread->id], 1);
	if (readyHead == NULL){
		exterminate();
        threadTracker[runningThread->id] = NULL;
        runningThread->wq->waitHead = NULL;
        runningThread->wq->size = 0;
        runningThread->wq = NULL;
		free(runningThread);
		runningThread = NULL;
		interrupts_set(enabled);
		exit(0);
	}
    Thread * nextThread = removeReady(readyHead->id);
    threadTracker[runningThread->id] = NULL;
	insertMurdered(runningThread);
	runningThread = nextThread;
	setcontext(&runningThread->threadContext);	
	interrupts_set(enabled);
}    

Tid
thread_kill(Tid tid)
{
    int enabled = interrupts_off();
    if (tid == runningThread->id || tid >= THREAD_MAX_THREADS || tid < 0 || threadTracker[tid] == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    threadTracker[tid]->state = MURDERED;
    Thread * ptr = removeReady(tid);
    if (ptr == NULL){
        if (threadTracker[tid] != NULL){
            threadTracker[tid]->state = MURDERED;
            threadTracker[tid]->threadContext.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_exit;
            interrupts_set(enabled);
            return tid;
        }
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
	insertMurdered(ptr);
    interrupts_set(enabled);
    return ptr->id;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
void insertWQ(Thread * insert, WQ * queue){
    queue->size = queue->size + 1;
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
 
void printWQ(WQ * queue){
	Thread * ptr = queue->waitHead;
    printf("wq:\n");
	while (ptr != NULL){
		printf("%d -> ", ptr->id);		
		ptr = ptr->nextThread;
	}
    printf("\n");
}

struct wait_queue *
wait_queue_create()
{
    WQ * wq;

    wq = malloc(sizeof (WQ));
    assert(wq);

    wq->waitHead = NULL;
    wq->size = 0;

    return wq;
}
 
void
wait_queue_destroy(struct wait_queue *wq)
{
    if (wq->waitHead == NULL){
        free(wq);
        return;
    }
    thread_wakeup(wq, 1);
    free(wq);
    return;
}
 
Tid
thread_sleep(struct wait_queue *queue)
{
    int enabled = interrupts_off();
    exterminate();
    if (queue == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    else if (readyHead == NULL){
        interrupts_set(enabled);
        return THREAD_NONE;
    }
    else {
        
        int runningThreadID;
        Thread * head = removeReady(readyHead->id);
        runningThreadID = head->id;
        insertWQ(runningThread, queue);
        flag = 0;
        getcontext(&runningThread->threadContext);
        if (runningThread->state == MURDERED){
            thread_exit();
        }
        if (threadTracker[runningThreadID] == NULL){
            interrupts_set(enabled);
            return runningThreadID;
        }
        if (flag == 0){
            flag = 1;
            runningThread = head;
            setcontext(&head->threadContext);
        }
        interrupts_set(enabled);
        return runningThreadID;
    }
    interrupts_set(enabled);
    return THREAD_INVALID;
}
 
/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    int enabled = interrupts_off();
    if (queue == NULL || queue->waitHead == NULL){
        interrupts_set(enabled);
        return 0;
    }
    else if (all == 0){
        queue->size = queue->size - 1;
        Thread * head = queue->waitHead;
        queue->waitHead = queue->waitHead->nextThread;
        head->nextThread = NULL;
        insertReady(head);
        interrupts_set(enabled);
        return 1;
    }
    else if (all == 1){
        int size = queue->size;
        insertReady(queue->waitHead);
        queue->waitHead = NULL;
        queue->size = 0;
        interrupts_set(enabled);
        return size;
    }
    interrupts_set(enabled);
    return 0;
}
 
/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
    int enabled = interrupts_off();
    if (tid == runningThread->id || threadTracker[tid] == NULL || tid < 0 || tid >= THREAD_MAX_THREADS){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    else {
        thread_sleep(&wq[tid]);
        exterminate();
        interrupts_set(enabled);
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
    struct lock * lock;
 
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
    int enabled = interrupts_off();
	while(lock->locked == 1){
		thread_sleep(lock->queue);
	}
    lock->locked = 1;
    interrupts_set(enabled);
}
 
void
lock_release(struct lock *lock)
{
    assert(lock != NULL);
    int enabled = interrupts_off();
    lock->locked = 0;
	thread_wakeup(lock->queue, 1);
    interrupts_set(enabled);
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
