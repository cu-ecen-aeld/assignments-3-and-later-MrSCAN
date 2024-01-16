#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg, ...)
// #define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg, ...) printf("threading ERROR: " msg "\n", ##__VA_ARGS__)

void *threadfunc(void *thread_param)
{

    // hint: use a cast like the one below to obtain thread arguments from your parameter
    // struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data *thread_func_args = (struct thread_data *)thread_param;
    usleep(thread_func_args->wait_to_obtain_ms * 1000); // Simulate waiting to obtain mutex
    pthread_mutex_lock(thread_func_args->mutex);        // Obtain mutex

    //Add Atomic code here

    usleep(thread_func_args->wait_to_release_ms * 1000); // Simulate waiting to release mutex
    pthread_mutex_unlock(thread_func_args->mutex);       // Release mutex

    thread_func_args->thread_complete_success = true;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *thread_args = (struct thread_data *)malloc(sizeof(struct thread_data));
    if (thread_args == NULL)
    {
        ERROR_LOG("Memory allocation failed");
        return false; // Memory allocation failed
    }

    // Set up thread arguments
    thread_args->mutex = mutex;
    thread_args->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_args->wait_to_release_ms = wait_to_release_ms;

    // Create thread
    if (pthread_create(thread, NULL, threadfunc, (void *)thread_args) != 0)
    {
        DEBUG_LOG("Free allocated memory if thread creation fails");
        free(thread_args); // Free allocated memory if thread creation fails
        return false;
    }

    return true;
}
