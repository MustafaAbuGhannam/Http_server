#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


threadpool* create_threadpool(int num_threads_in_pool){
    if(num_threads_in_pool < 0 || num_threads_in_pool > MAXT_IN_POOL){
        fprintf(stderr,"bad threads count\n");
        return NULL;
    }

    int result = 0;
    int i;

    threadpool* pool = (threadpool*) malloc(1 * sizeof(threadpool));
    if(pool == NULL){
        perror("malloc failed");
        return NULL;
    }
    pool->num_threads = num_threads_in_pool;
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads_in_pool);
    if(pool->threads == NULL){
        perror("malloc failed");
        free(pool);
        return NULL;
    }
    result = pthread_mutex_init(&pool->qlock, NULL);
    if(result != 0){
        perror("pthread_mutex_init failed");
        free(pool->threads);
        free(pool);
        return NULL;
    }
    result = pthread_cond_init(&pool->q_not_empty,NULL);
    if(result != 0){
        perror("pthread_cond_init failed");
        free(pool->threads);
        free(pool);
        return NULL;
    }
    result = pthread_cond_init(&pool->q_empty, NULL);
    if(result != 0){
        perror("pthread_cond_init failed");
        free(pool->threads);
        free(pool);
        return NULL;
    }

    pool->qhead = NULL;
    pool->qtail = NULL;
    pool->qsize = 0;
    pool->shutdown = 0;
    pool->dont_accept = 0;

    for(i = 0; i < num_threads_in_pool; i++){
       result = pthread_create(pool->threads + i, NULL, do_work, pool);
       if(result != 0){
           perror("pthread_create failed");
           free(pool->threads);
           free(pool);
           return NULL;
       }
    }

    return pool;


}


void* do_work(void* p){
    int returnVal;
    threadpool* pool = (threadpool*)p;
    returnVal = pthread_mutex_lock(&pool->qlock);
    if(returnVal != 0){
        return NULL;
    }
    while(1){
        if(pool->shutdown == 1){
           returnVal = pthread_mutex_unlock(&pool->qlock);
            return NULL;
        }

        else if(pool->qsize == 0){
            returnVal = pthread_cond_wait(&pool->q_not_empty, &pool->qlock);

           if(returnVal != 0){
               perror("ptherad_cond_wait failed");
               return NULL;
           }
        }

        else{
            work_t* work = pool->qhead;
            pool->qhead = pool->qhead->next;
            pool->qsize -= 1;

            if(pool->qsize == 0 && pool->dont_accept == 1){
               returnVal = pthread_cond_signal(&pool->q_empty);
               if(returnVal != 0){
                   return NULL;
               }
                pthread_mutex_unlock(&pool->qlock);
                work->routine(work->arg);
                free(work);
                return NULL;
            }
            pthread_mutex_unlock(&pool->qlock);
            work->routine(work->arg);
            free(work);
             returnVal = pthread_mutex_lock(&pool->qlock);
             if(returnVal != 0){
                 return NULL;
             }
        }
    }
    

}


void destroy_threadpool(threadpool* destroyme){
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = 1;
    if(destroyme->qsize != 0){
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    }
    destroyme->shutdown = 1;
    pthread_mutex_unlock(&destroyme->qlock);
    pthread_cond_broadcast(&destroyme->q_not_empty);
    
    
    int i;
    int len = destroyme->num_threads;
    for(i = 0; i < len; i++){
        pthread_join(destroyme->threads[i], NULL);
    }
    
    pthread_cond_destroy(&destroyme->q_empty);
    pthread_cond_destroy(&destroyme->q_not_empty);
    pthread_mutex_destroy(&destroyme->qlock);
    free(destroyme->threads);
    free(destroyme);



}


void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg){
    if(from_me == NULL){
        printf("uninisilized value\n");
        return;
    }
    if(from_me->dont_accept == 1){
        return;
    }
    work_t* work = (work_t*)malloc(sizeof(work_t) * 1);
    if(work == NULL){
        perror("malloc failed");
        destroy_threadpool(from_me);
        return;
    }
    work->arg = arg;
    work->next = NULL;
    work->routine = dispatch_to_here;

    pthread_mutex_lock(&from_me->qlock);
    
    if(from_me->qsize == 0){
        from_me->qhead = work;
        from_me->qtail = work;
        from_me->qsize += 1;
    }
    else{
        from_me->qtail->next = work;
        from_me->qtail = work;
        from_me->qsize += 1;
    }
    pthread_mutex_unlock(&from_me->qlock);
    pthread_cond_signal(&from_me->q_not_empty);
    
    return;

}