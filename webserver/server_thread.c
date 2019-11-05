#include "request.h"
#include "server_thread.h"
#include "common.h"

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	int * buffer;
	int in;
	int out;
	pthread_mutex_t * lock;
	pthread_cond_t * empty;
	pthread_cond_t * full;
	pthread_t * tid;
	/* add any other parameters you need */
};

void server_response(struct server *sv);

/* static functions */

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
	sv->lock = malloc(sizeof(pthread_mutex_t));
	sv->empty = malloc(sizeof(pthread_cond_t));
	sv->full = malloc(sizeof(pthread_cond_t));
	pthread_mutex_init(sv->lock, NULL);
	pthread_cond_init(sv->empty, NULL);
	pthread_cond_init(sv->full, NULL);	
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		/* Lab 4: create queue of max_request size when max_requests > 0 */
		sv->buffer = malloc(sizeof(int) * max_requests);
		/* Lab 5: init server cache and limit its size to max_cache_size */

		/* Lab 4: create worker threads when nr_threads > 0 */
		sv->tid = malloc(sizeof(pthread_t) * nr_threads);
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
		//if (sv->in == sv->out){
			pthread_cond_broadcast(sv->empty);
		//}
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
		//if ((sv->in - sv->out + sv->max_requests) % (sv->max_requests) == (sv->max_requests - 1)){
			pthread_cond_broadcast(sv->full);
		//}
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

	/* make sure to free any allocated resources */
	free(sv->tid);
	free(sv->full);
	free(sv->empty);
	free(sv->lock);
	free(sv);
}

