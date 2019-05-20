/* This program gets a path for a root, a search term and a number of threads. Those threads will search for the search term at the directories starting at the root. 
We will use a Queue as our main data structure. */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h> 
#include <signal.h>

/* Structure for the queue */ 
typedef struct Queue { 
    int size; 
    char ** array; 
} Queue; 

pthread_mutex_t  lock;
pthread_cond_t empty_cv;
int sleepingThreadsCounter = 0;
int numberOfThreads;
int totalFilesFound = 0;
int stopped = 0;
int threadCollapsed = 0;
Queue* q;
 
void handler () {
	stopped = 1; /* Program Got a SIGINT */
}

/* Creates the Queue */
Queue* createQueue() { 
    Queue* queue = (struct Queue*) malloc(sizeof(struct Queue)); 
    if (queue == NULL) {
    	printf("malloc ERROR \n");
		return NULL;
    }
    queue->size = 0; 
    queue->array = NULL; 
    return queue; 
} 
/* checks if the Queue is empty */
int isEmpty(Queue* queue) {  
    return (queue->size == 0); 
} 
  
/* Adds an item to the Queue (FIFO) */ 
int enqueue(Queue* queue, char * pathName) {
    queue->size = queue->size + 1; 
    queue->array = (char**) realloc(queue->array, sizeof(char*) * (queue->size)); 
    if (queue->array == NULL) {
    	printf("malloc ERROR for thread \n");
    	threadCollapsed = 1;
		return -1;
    }
    queue->array[queue->size-1] = (char*)malloc(sizeof(char)*(strlen(pathName) + 5));
    if (queue->array[queue->size-1] == NULL) {
    	printf("malloc ERROR for thread \n");
    	threadCollapsed = 1;
		return -1;
    }
    for (int i=0; i<strlen(pathName); i++) {
    	queue->array[queue->size-1][i] = pathName[i]; /* adda the path name to the Queue item */
    }
    queue->array[queue->size-1][strlen(pathName)] = '\0';
    //printf("%s enqueued to queue\n", pathName); 
    return 0;
} 
  
/* Removes an item from the Queue */
char* dequeue(Queue* queue) { 
	char* tmp;
    if (isEmpty(queue)) {
        return NULL; 
    }
	char* item = (char*)malloc(sizeof(char)*(strlen(queue->array[0]) + 2));
	if (item == NULL) {
    	printf("malloc ERROR for thread \n");
    	threadCollapsed = 1;
		return NULL;
    }
	for (int i=0; i<strlen(queue->array[0]); i++) {
		item[i] = queue->array[0][i];
	}
	item[strlen(queue->array[0])] ='\0';
    if (queue->size == 1) {
    	free(queue->array[0]);
    }
    for (int i = 0; i < queue->size - 1; i++) {
    	queue->array[i] = (char*)realloc(queue->array[i],sizeof(char)*strlen(queue->array[i+1]) + 5);
    	if (queue->array[i] == NULL) {
    		printf("malloc ERROR for thread \n");
    		threadCollapsed = 1;
			return NULL;
    	}
    	for (int j=0;j<strlen(queue->array[i+1]);j++) {
			queue->array[i][j] = queue->array[i+1][j];
    	}
    	    queue->array[i][strlen(queue->array[i+1])] = '\0';
    }
    if (queue->size > 1) {
    	tmp = queue->array[queue->size-1];
    	free(tmp);
    }
    queue->size = queue->size - 1; 
    queue->array = (char**) realloc(queue->array, sizeof(char*) * (queue->size)); 
    if (queue->size > 0 && queue->array == NULL) {
    	printf("malloc ERROR for thread \n");
    	threadCollapsed = 1;
		return NULL;
    }
    //printf("%s dibug dequeued to queue\n", item); 
    return item; 
} 

/* Frees the Queue */
void freeQueue (Queue* queue) {
	char* tmp;
    while (isEmpty(q) == 0) {
        tmp = dequeue(queue);
        if (tmp != NULL) {
        	free(tmp);
        }
    }
    free(queue->array);
    free(queue);
}

/* The main method for searching by the threads */
void* takeAction (void* input) {
	DIR* dirp;
	struct dirent* dp;
	char* pathName;
	char* searchTerm = (char*) input;
	int rc;
	char* str;

	while (1) {
		rc = pthread_mutex_lock(&lock);
		if(rc != 0) {
			fprintf(stderr, "ERROR in pthread_mutex_lock(): %s\n", strerror(rc));
			threadCollapsed = 1;
			exit(1);
		}
		while (isEmpty(q)) {
			sleepingThreadsCounter++;
			if (sleepingThreadsCounter == numberOfThreads) {
				rc = pthread_mutex_unlock(&lock);
				if(rc != 0) {
					fprintf(stderr, "ERROR in pthread_mutex_unlock(): %s\n", strerror(rc));
					threadCollapsed = 1;
					exit(1);
				}
				pthread_cond_signal(&empty_cv);
				return NULL;
				//pthread_exit((void*)0); causes valgrind problems. It was written in the forum to change to return NULL
			}
			pthread_cond_wait(&empty_cv,&lock); /* sleeps until other thread sends a signal on empty_cv. wait unlock atomiclly and automaticly*/
			sleepingThreadsCounter--;
		}

		if (stopped == 1) {
			sleepingThreadsCounter++;
			rc = pthread_mutex_unlock(&lock);
			if(rc != 0) {
				fprintf(stderr, "ERROR in pthread_mutex_unlock(): %s\n", strerror(rc));
				threadCollapsed = 1;
				exit(1);
			}
			pthread_cond_signal(&empty_cv);
			return NULL;
		}

		pathName = dequeue(q);
		if (pathName == NULL) { /*thread collapsed*/
			__sync_fetch_and_add(&sleepingThreadsCounter, 1);
			pthread_cond_signal(&empty_cv);
			pthread_exit((void*)-1);
		}
		rc = pthread_mutex_unlock(&lock);
		if(rc != 0) {
			fprintf(stderr, "ERROR in pthread_mutex_unlock(): %s\n", strerror(rc));
			threadCollapsed = 1;
			exit(1);
		}
		dirp = opendir(pathName);
		if (dirp != NULL) {
			while ((dp = readdir(dirp)) != NULL) {
				if (stopped == 1) {
					__sync_fetch_and_add(&sleepingThreadsCounter, 1);
					pthread_cond_signal(&empty_cv);
					return NULL;
				}
				if (strcmp(dp->d_name,"..") == 0 || strcmp(dp->d_name,".") ==0 ) {
					continue;
				}
		        if (dp->d_type == DT_REG) { /* regular file */
					//sleep(1); 
					if (strstr(dp->d_name, searchTerm) != NULL) { /* contains */
						str = (char*) malloc (sizeof(char) *(strlen(pathName)+strlen(dp->d_name) + 5));
						if (str == NULL) {
							fprintf(stderr, "ERROR in ,malloc() for thread: %s\n", strerror(rc));
							threadCollapsed = 1;
							__sync_fetch_and_add(&sleepingThreadsCounter, 1);
							pthread_cond_signal(&empty_cv);
							return NULL;
						}
						__sync_fetch_and_add(&totalFilesFound, 1); //adds 1 to the counter atomiclly
						for (int i=0 ;i<strlen(pathName); i++) {
							str[i] = pathName[i];
						}
						str[strlen(pathName)]='/';
						for(int i=0;i<strlen(dp->d_name);i++) {
							str[strlen(pathName)+1+i] = dp->d_name[i];
						}
						str[strlen(pathName)+strlen(dp->d_name)+1] = '\0';
						printf("%s\n",str);
						free(str);
					}
				}
		        if (dp->d_type == DT_DIR) { /* directory file */
					str = (char*) malloc (sizeof(char) *(strlen(pathName)+strlen(dp->d_name) + 5));
					if (str == NULL) {
						fprintf(stderr, "ERROR in ,malloc() for thread: %s\n", strerror(rc));
						threadCollapsed = 1;
						__sync_fetch_and_add(&sleepingThreadsCounter, 1);
						pthread_cond_signal(&empty_cv);
						return NULL;
					}
					for (int i=0 ;i<strlen(pathName); i++) {
						str[i] = pathName[i];
					}
					str[strlen(pathName)]='/';
					for(int i=0;i<strlen(dp->d_name);i++) {
						str[strlen(pathName)+1+i] = dp->d_name[i];
					}
					str[strlen(pathName)+strlen(dp->d_name)+1] = '\0';
					rc = pthread_mutex_lock(&lock);
					if(rc != 0) {
						fprintf(stderr, "ERROR in pthread_mutex_lock(): %s\n", strerror(rc));
						threadCollapsed = 1;
						exit(1);
					}
					if (enqueue(q,str) == -1) {
						__sync_fetch_and_add(&sleepingThreadsCounter, 1);
						pthread_cond_signal(&empty_cv);
						pthread_exit((void*)-1);
					}
					rc = pthread_mutex_unlock(&lock);
					if(rc != 0) {
						fprintf(stderr, "ERROR in pthread_mutex_unlock(): %s\n", strerror(rc));
						threadCollapsed = 1;
						exit(1);
					}
					pthread_cond_signal(&empty_cv);
					free(str);
				}
			}
		}
		if (pathName != NULL) {
			free(pathName);
		}
		closedir(dirp);
	}
	return ((void*)0);
}

int main (int argc, char * argv[]) {
	struct sigaction Stopper ;
	memset(&Stopper, '\0',sizeof(Stopper));
	Stopper.sa_handler = handler;
	sigaction(SIGINT,&Stopper,NULL);

	int tmp;
	void* status;
	q = createQueue();
	if (q == NULL) {
		return 1;
	}
	if (argc < 4) {
		printf("ERROR : missing arguements \n");
		exit(-1);
	}
	char * root = argv[1];
	char * searchTerm = argv[2];
	numberOfThreads = atoi(argv[3]);
	int rc = 0;

// --- Initialize mutex and condition variable objects ----------------------------  
	pthread_t * thread_ids = (pthread_t*)calloc(numberOfThreads,sizeof(pthread_t));
	rc = pthread_mutex_init(&lock, NULL);
	if(rc != 0) {
		fprintf(stderr, "ERROR in pthread_mutex_init: %s\n", strerror(rc));
		exit(-1);
	}

	rc = pthread_cond_init(&empty_cv, NULL);
	if (rc != 0) {
		fprintf(stderr, "ERROR in pthread_cond_init: %s\n", strerror(rc));
		exit(-1);
	}
  	enqueue(q, root); /*we add the root to the Queue*/


// --- Launch threads ------------------------------

	for (int i=0; i < numberOfThreads; i++) {
		rc = pthread_create(&thread_ids[i], NULL, takeAction, (void *)searchTerm);
		if (rc != 0) {
			fprintf(stderr, "Failed creating thread: %s\n", strerror(rc));
			exit(-1);
		}
	}
// --- Wait for threads to finish ------------------

	for (int i = 0; i < numberOfThreads; i++) {
		pthread_join(thread_ids[i], &status);
		if (rc) {
			fprintf(stderr, "Failed pthread_join: %s\n", strerror(rc));
			exit(-1);
		}
		tmp = (int)status;
		if (tmp != 0) {
			threadCollapsed = 1;
		}
		//printf("Main: completed join with thread %ld having a status of %ld\n",(long)i,(long)status);
	}
// --- Epilogue -------------------------------------
	if (stopped == 0) {
		printf("Done searching, found %d files\n", totalFilesFound);
	} else {
		printf("Search stopped, found %d files.\n", totalFilesFound);
	}
	if (thread_ids != NULL) {
		free(thread_ids);
	}
	pthread_mutex_destroy(&lock);
	pthread_cond_destroy(&empty_cv);
	freeQueue(q);
	return threadCollapsed;
}
//=================== END OF FILE ====================
