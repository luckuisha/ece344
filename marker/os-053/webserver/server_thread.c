#include "request.h"
#include "server_thread.h"
#include "common.h"

struct server {
	int nr_threads;	//number of threads
	int max_requests;	//number of requests
	int max_cache_size;	//max cache size
	int exiting;	//determines whether program should exit or not
	int * buffer;	
	int in;	//read value location
	int out;	//send value location
	pthread_mutex_t * lock;	//lock for the server
	pthread_cond_t * empty;	//when full, the threads that are waiting queue
	pthread_cond_t * full;	//when empty, ^     ^     ^    ^     ^      ^
	pthread_t * tid;	//holds a pointer to the thread ids
	/* add any other parameters you need */
};

//main cache structure
struct cash { 
	struct node ** cashTable; //cache table, holds head of the hash linked list for hash values (array of linked lists)
	int size;	//total size of cache table
	int cashAvailable;	//available memory in cache
	pthread_mutex_t * safe;	//lock for accessing the cache table
	struct node * counterfeitCash;	//pointer to head of linked list which holds all of inserted files
};

//data entry for the linked list
typedef struct node {
	struct file_data * file;	//holds the actual file
	struct node * nextNode;	//holds the pointer to the next item in linked list
	int users;	//number of threads currently using the data (this variable is only updated within the cache, not the LRU even though both the LRU and cache use this struct)
}Node;

//globals
struct cash * Cash;	//main cache table

/* static functions */
void server_response(struct server *sv);	//threads all reading the passed files
unsigned long hash(char *str);	//hash function
Node * lookup_cash(struct file_data * file);	//lookups in the cache for a specific file
void insert_cash_table(struct file_data * file); 	//inserts a file into the cache table
void insert_cash(struct file_data *file);	//determines whether to evict prior to inserting into cache table
void evict_cash(int amount_to_evict);	//evicts the largest node that takes up the most memory in the cache table
void spend_cash(struct file_data *file);	//removes and frees nodes from the cache table
void insert_latest_cash_use(struct file_data * file);	//inserts file into linked list lru at tail, if exists, return
void go_bankrupt();	//deletes and frees cache table
void burn_cash_trace();	//deletes and frees LRU linked list
void printcash();	//prints the cache and the LRU (mostly for debugging)

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	Node * cacheData = NULL;
	if (sv->max_cache_size > 0){	//checks if size of the cache greater than 0
		pthread_mutex_lock(Cash->safe);	//since reading through cache, lock the data
		cacheData = lookup_cash(data);	//check if the data exists or not
		if (cacheData != NULL){	//if it does, update the data of the request and send the data
			cacheData->users++;	//since someone is reading through the data in the cache, increment users
			request_set_data(rq, cacheData->file);	//update data
			pthread_mutex_unlock(Cash->safe);

			request_sendfile(rq);

			pthread_mutex_lock(Cash->safe);
			cacheData->users--;	//decrement users since we are no longer reading the data, decrement
			request_destroy(rq);
			pthread_mutex_unlock(Cash->safe);

			return;
		}
		pthread_mutex_unlock(Cash->safe);
		//if the data does not yet exist:
		request_readfile(rq);	//read
		request_sendfile(rq);	//send

		pthread_mutex_lock(Cash->safe);
		cacheData = lookup_cash(data);	//check again
		if (cacheData == NULL){
			insert_cash(data);	//insert into the cache
			insert_latest_cash_use(data);	//as well as into the lru
		}
		else {
			file_data_free(data);
		}
		request_destroy(rq);
		pthread_mutex_unlock(Cash->safe);
		return;
	}

	else {	//if cache size = 0, use given function 
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
	out:
		request_destroy(rq);
		file_data_free(data);
	}
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	sv->in = 0;
	sv->out = 0;
	sv->lock = Malloc(sizeof(pthread_mutex_t));
	sv->empty = Malloc(sizeof(pthread_cond_t));
	sv->full = Malloc(sizeof(pthread_cond_t));
	pthread_mutex_init(sv->lock, NULL);
	pthread_cond_init(sv->empty, NULL);
	pthread_cond_init(sv->full, NULL);
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		/* Lab 4: create queue of max_request size when max_requests > 0 */
		sv->buffer = Malloc(sizeof(int) * max_requests);
		/* Lab 5: init server cache and limit its size to max_cache_size */
		if (max_cache_size > 0){
			Cash = Malloc (sizeof(struct cash));
			Cash->safe = Malloc(sizeof(pthread_mutex_t));
			pthread_mutex_init(Cash->safe, NULL);
			Cash->size = max_cache_size + 1;
			Cash->cashAvailable = max_cache_size;
			Cash->cashTable = (Node **)Malloc(Cash->size * sizeof(Node * ));
			Cash->counterfeitCash = NULL;
			for (int i = 0; i < Cash->size; i++){
				Cash->cashTable[i] = NULL;
			}
		}
		
		/* Lab 4: create worker threads when nr_threads > 0 */
		sv->tid = Malloc(sizeof(pthread_t) * nr_threads);
		for (int i = 0; i < nr_threads; i++){
			pthread_create(&sv->tid[i], NULL, (void *)&server_response, sv);
		}
	}

	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(sv->lock);
		while ((sv->in - sv->out + sv->max_requests) % sv->max_requests == (sv->max_requests - 1)) {
			pthread_cond_wait(sv->full, sv->lock);
		} //full
		sv->buffer[sv->in] = connfd;
		pthread_cond_broadcast(sv->empty);
		sv->in = (sv->in + 1) % sv->max_requests;
		pthread_mutex_unlock(sv->lock);
	}
}

void server_response(struct server * sv){
	while (1){
		pthread_mutex_lock(sv->lock);
		while (sv->in == sv->out) {
			if (sv->exiting){
				pthread_mutex_unlock(sv->lock);
				return;
			}
			pthread_cond_wait(sv->empty, sv->lock);
		} //empty
		int connfd = sv->buffer[sv->out];
		pthread_cond_broadcast(sv->full);
		sv->out = (sv->out + 1) % sv->max_requests;
		pthread_mutex_unlock(sv->lock);
		do_server_request(sv, connfd);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	pthread_cond_broadcast(sv->full);
	pthread_cond_broadcast(sv->empty);

	for (int i = 0; i < sv->nr_threads; i++){
		pthread_join(sv->tid[i], NULL);
	}

	if (sv->max_cache_size > 0){
		go_bankrupt();
		burn_cash_trace();
	}
	/* make sure to free any allocated resources */
	free(sv->tid);
	free(sv->full);
	free(sv->empty);
	free(sv->lock);
	free(sv);
	
}

unsigned long hash(char *str)
{
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = hash * 33 ^ c;

	return hash % Cash->size;
}

Node * lookup_cash(struct file_data * file){
	long index = hash(file->file_name);
	if (Cash->cashTable[index] == NULL){
		return NULL;
	}
	Node * ptr = Cash->cashTable[index];
	while (ptr != NULL){
		if (strcmp(ptr->file->file_name, file->file_name) == 0){
			return ptr;
		}
		ptr = ptr->nextNode;
	}
	return NULL;
}

void insert_cash_table(struct file_data * file){
	long index = hash(file->file_name);
	Node * newNode = Malloc(sizeof(Node));
	newNode->file = file;
	newNode->nextNode = NULL;
	newNode->users = 0;
	if (Cash->cashTable[index] == NULL){	//if nothing there
		Cash->cashTable[index] = newNode;
	}
	else {	//if there is an item, linked list stuff here
		Node * ptr = Cash->cashTable[index];
		while (ptr->nextNode != NULL){
			ptr = ptr->nextNode;
		}
		ptr->nextNode = newNode;
	}
	Cash->cashAvailable -= file->file_size;	//decrement the available cache since file was added
}

void insert_cash(struct file_data *file){
	if (Cash->cashAvailable < file->file_size){ //if file is too big to fit the cache
		evict_cash(file->file_size);	//remove some usless stuff from the cache (lru)
	}
	insert_cash_table(file);	
}

void evict_cash(int amount_to_evict){
	Node * remove = Cash->counterfeitCash;
	Node * prev = Cash->counterfeitCash;
	while (Cash->cashAvailable < amount_to_evict){
		Node * cash = lookup_cash(remove->file);	//lookup to see if there are any users (this is required since users arent updated within the lru)
		if (cash->users == 0){	//checks if anyone is reading through a file in the cache 
			if(remove == Cash->counterfeitCash){	//if at head
				Cash->counterfeitCash = remove->nextNode;	//do linked list stuff
				prev = Cash->counterfeitCash;
				spend_cash(remove->file);	//remove it from cache
				Cash->cashAvailable += remove->file->file_size;	//increase available cache now the file is removed
				free(remove);	//free it
				remove = prev;	//reset the pointers so that more can be freed at next loop
				continue;	//this continue is here to recheck the head now that the new head is different
			}
			else {
				prev->nextNode = remove->nextNode;
				spend_cash(remove->file);
				Cash->cashAvailable += remove->file->file_size;
				free(remove);
				remove = prev;	//same as above but no need to use continue since the next one will be checked by going to the next pointer (below)
			}
		}
		prev = remove;
		remove = remove->nextNode;
	}
}

void spend_cash(struct file_data *file){
	long index = hash(file->file_name);
	if (Cash->cashTable[index] == NULL){
		return;
	}
	Node * ptr = Cash->cashTable[index];
	Node * prev = Cash->cashTable[index];
	while (strcmp(ptr->file->file_name, file->file_name) != 0){ //find the node with same file
		prev = ptr;
		ptr = ptr->nextNode;
	}
	if (ptr != NULL){	//if it exists, delete by doing linked list nonsense
		if (ptr == Cash->cashTable[index]){
			Cash->cashTable[index] = ptr->nextNode;
			free(ptr);
			return;
		}
		prev->nextNode = ptr->nextNode;
		free(ptr);
		return;
	}
}

void insert_latest_cash_use(struct file_data * file){	//insert lru to tail
	if (Cash->counterfeitCash == NULL){	//if LRU is empty, insert into LRU
		Node * newNode = Malloc(sizeof(Node));
		newNode->file = file;
		newNode->users = 0;
		newNode->nextNode = NULL;
		Cash->counterfeitCash = newNode;
		return;
	}
	else {	//if multiple in LRU
		Node * ptr = Cash->counterfeitCash;
		while (ptr->nextNode != NULL){
			if (strcmp(ptr->file->file_name, file->file_name) == 0){	//if same item exists, exit
				return;
			}
			ptr = ptr->nextNode;
		}
		if (strcmp(ptr->file->file_name, file->file_name) == 0){	//makes sure to theck the list item since while loop exits before checking last item
			return;
		}
		Node * newNode = Malloc(sizeof(Node));
		newNode->file = file;
		newNode->users = 0;
		newNode->nextNode = NULL;
		ptr->nextNode = newNode;
	}
}

void go_bankrupt(){
	for (int i = 0; i < Cash->size; i++){
		Node * ptr = Cash->cashTable[i];
		Node * ptrNext = Cash->cashTable[i];
		while(ptr != NULL){
			ptrNext = ptr->nextNode;
			free(ptr);
			ptr = ptrNext;
		}
	}
	free(Cash->cashTable);
	free(Cash->safe);
	free(Cash);
}

void burn_cash_trace(){
	Node * ptr = Cash->counterfeitCash;
	Node * ptrNext = Cash->counterfeitCash;
	while(ptr != NULL){
		ptrNext = ptr->nextNode;
		free(ptr);
		ptr = ptrNext;
	}
}

void printcash(){
	for (int i = 0; i<Cash->size; i++){
		Node * ptr = Cash->cashTable[i];
		while (ptr != NULL){
			printf("%s ->", ptr->file->file_name);
			ptr=ptr->nextNode;
		}
	}
	printf("\n");

	Node * ptr = Cash->counterfeitCash;
	while (ptr != NULL){
		printf("%s ->", ptr->file->file_name);
		ptr=ptr->nextNode;
	}
	printf("\n");
}