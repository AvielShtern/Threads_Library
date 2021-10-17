

#include "uthreads.h"
#include "Thread.hpp"
#include <ctime>
#include <iostream>
#include <list>
#include <sys/time.h>
#include <algorithm>
#include "ThreadsCollectionManager.hpp"
#include <functional>


#define FAILURE -1
#define SUCCESS 0
#define ERR_INIT "Non positive quantum_usecs. "
#define SYS_ERROR_MSG "system error: "
#define LIB_ERROR_MSG "thread library error: "
#define ERR_SIG "Error in signal handling."
#define MAX_THREADS "No place for more threads."
#define MASK_ERROR "Error masking signals."
#define BAD_ALLOC "Memory allocation failed, (consider buy a new computer)."
#define MUTEX_LOCK_TWICE "You already have the mutex, you probably lost it somewhere."
#define ID_NOT_FOUND "A thread with the given id does not exist. or it's illegal to block this thread. "
#define MUTEX_UNLOCKED "Can't unblock mutex. "


using std::string;
using std::endl;
using std::cerr;
using std::function;


/**
 * The mutex object.
 */
struct Mutex{
    bool locked = false;
    int locking_thread = -1;
};



/**
 * A signal handler for SIGVTALARN.
 * @param sig
 */
void time_sig_handler(int sig);


/**
 * Set timer alarm (setitimer with error checking).
 */
void set_timer();

/**
 * Save context and jump to new thread execution.
 * @param handle_curr_thread A function to run execute before jumping to take care of old thread.
 */
void switch_threads( const function<void()>&handle_curr_thread);

/**
 * Switch threads in the middle of quantum (wraps switch threads).
 * @param handle_curr_thread
 */
void switch_threads_mid_quantum(const function<void()>& handle_curr_thread);


/**
 * Initialize the timer struct with usecs (for setitimer).
 * @param usecs
 */
void init_timer(int usecs);


/**
 * A wrapper around sigprocmask with error checking
 * @param how Arg to pass to sigprocmask
 */
void mask_time_signal(int how);


// --------- Static variables ---------------

static struct sigaction time_handler = {time_sig_handler};

static size_t total_quantums;

static struct itimerval timer;

static ThreadsCollectionManager threadsCollectionManager(MAX_THREAD_NUM, STACK_SIZE);

static sigset_t sigvtalarm;

static Mutex mutex;


// --------- Libraries public functions ---------------


/**
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs){
    if (quantum_usecs <= 0){
        cerr << LIB_ERROR_MSG << ERR_INIT << endl;
        return FAILURE;
    }
    init_timer(quantum_usecs);
    bool sys_calls_err = (sigaction(SIGVTALRM, &time_handler ,nullptr) < 0 ||
                     sigemptyset(&sigvtalarm) < 0 ||     sigaddset(&sigvtalarm, SIGVTALRM) < 0);
    if (sys_calls_err) {
        cerr << SYS_ERROR_MSG << ERR_SIG << endl;
        exit(EXIT_FAILURE);
    }
    total_quantums++;
    set_timer();
    return SUCCESS;
}


/**
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void)){
    int id;
    try {
        id = threadsCollectionManager.create_thread(f);
    } catch (const std::bad_alloc& e) {
        cerr << SYS_ERROR_MSG << BAD_ALLOC << endl;
        std::exit(EXIT_FAILURE);
    }
    if (id == FAILURE){
        cerr << LIB_ERROR_MSG << MAX_THREADS << endl;
    }
    return id;
}


/**
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid){
    mask_time_signal(SIG_BLOCK);
    if (tid == 0){
        std::exit(EXIT_SUCCESS);
    }
    if (!threadsCollectionManager.contains(tid)){
        cerr << LIB_ERROR_MSG << ID_NOT_FOUND << endl;
        sigprocmask(SIG_UNBLOCK, &sigvtalarm, nullptr);
        return FAILURE;
    }
    function<void()> delete_thread = [tid] () {
        threadsCollectionManager.terminate(tid);
        if (mutex.locking_thread == tid){
            mutex.locking_thread = -1;
            mutex.locked = false;
            threadsCollectionManager.advance_mutex_line();
        }
    };
    if (tid == threadsCollectionManager.get_curr_id()){
        switch_threads_mid_quantum(delete_thread);
    }
    delete_thread();
    mask_time_signal(SIG_UNBLOCK);
    return SUCCESS;
}



/**
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid){
    mask_time_signal(SIG_BLOCK);
    if (tid == 0 || !threadsCollectionManager.contains(tid)){
        cerr << LIB_ERROR_MSG << ID_NOT_FOUND << endl;
        mask_time_signal(SIG_UNBLOCK);
        return FAILURE;
    }
    if (threadsCollectionManager.get_curr_id() == tid){
        switch_threads_mid_quantum([tid](){threadsCollectionManager.block(tid); });
    } else {
        threadsCollectionManager.block(tid);
    }
    mask_time_signal(SIG_UNBLOCK);
    return SUCCESS;
}


/**
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid){
    mask_time_signal(SIG_BLOCK);
    int success = threadsCollectionManager.resume(tid);
    if (success == FAILURE) {
        cerr << LIB_ERROR_MSG << ID_NOT_FOUND << endl;
    }
    mask_time_signal(SIG_UNBLOCK);
    return success;
}


/**
 * Description: This function tries to acquire a mutex.
 * If the mutex is unlocked, it locks it and returns.
 * If the mutex is already locked by different thread, the thread moves to BLOCK state.
 * In the future when this thread will be back to RUNNING state,
 * it will try again to acquire the mutex.
 * If the mutex is already locked by this thread, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_lock(){
    mask_time_signal(SIG_BLOCK);
    if (mutex.locking_thread == threadsCollectionManager.get_curr_id()) {
        cerr << LIB_ERROR_MSG << MUTEX_LOCK_TWICE << endl;
        mask_time_signal(SIG_UNBLOCK);
        return FAILURE;
    }
    while (mutex.locked){
        int id = threadsCollectionManager.get_curr_id();
        switch_threads_mid_quantum([id](){
            threadsCollectionManager.wait_for_mutex(id);});
    }
    mutex.locked = true;
    mutex.locking_thread = threadsCollectionManager.get_curr_id();
    mask_time_signal(SIG_UNBLOCK);
    return SUCCESS;
}



/**
 * Description: This function releases a mutex.
 * If there are blocked threads waiting for this mutex,
 * one of them (no matter which one) moves to READY state.
 * If the mutex is already unlocked, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_unlock(){
    mask_time_signal(SIG_BLOCK);
    if (!mutex.locked || mutex.locking_thread != threadsCollectionManager.get_curr_id()){
        cerr << LIB_ERROR_MSG << MUTEX_UNLOCKED << endl;
        mask_time_signal(SIG_UNBLOCK);
        return FAILURE;
    }
    mutex.locked = false;
    mutex.locking_thread = -1;
    threadsCollectionManager.advance_mutex_line();
    mask_time_signal(SIG_UNBLOCK);
    return SUCCESS;
}


/**
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid(){
    return threadsCollectionManager.get_curr_id();
}


/**
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums(){
    return total_quantums;
}


/**
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid){
    mask_time_signal(SIG_BLOCK);
    if (!threadsCollectionManager.contains(tid)){
        cerr << LIB_ERROR_MSG << ID_NOT_FOUND << endl;
        return FAILURE;
    }
    int quantums = threadsCollectionManager.get_thread(tid).quantums;
    mask_time_signal(SIG_UNBLOCK);
    return quantums;
}

// --------- helper functions ---------------


void init_timer(int usecs){
    struct timeval time_val{};
    time_val.tv_sec = usecs / 1000000;
    time_val.tv_usec = usecs % 1000000;
    timer.it_value = time_val;
    timer.it_interval = time_val;
}


void time_sig_handler(int sig){
    if (!threadsCollectionManager.is_someone_waiting()){
        total_quantums++;
        threadsCollectionManager.get_current_thread().quantums++;
        return;
    }
    int curr_id = threadsCollectionManager.get_curr_id();
    switch_threads([curr_id]() {threadsCollectionManager.set_as_ready(curr_id);});
};


void switch_threads(const function<void()>& handle_curr_thread){
    total_quantums++;
    int ret_val = sigsetjmp(threadsCollectionManager.get_current_thread().env, 1);
    if (ret_val == 1) {
        return;
    }
    threadsCollectionManager.set_next_thread_as_running();
    handle_curr_thread();
    threadsCollectionManager.get_current_thread().quantums++;
    siglongjmp(threadsCollectionManager.get_current_thread().env,1);
}


void switch_threads_mid_quantum(const function<void()>& handle_curr_thread){
    set_timer();
    switch_threads(handle_curr_thread);
}

void mask_time_signal(int how){
    if (sigprocmask(how, &sigvtalarm, nullptr) < 0){
        cerr << SYS_ERROR_MSG << MASK_ERROR << endl;
        std::exit(EXIT_FAILURE);
    }
}


void set_timer(){
    if (setitimer (ITIMER_VIRTUAL, &timer, nullptr) < 0) {
        cerr << SYS_ERROR_MSG << ERR_SIG << endl;
        exit(EXIT_FAILURE);
    }
}

