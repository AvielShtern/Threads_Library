#ifndef EX2_THREADSCOLLECTIONMANAGER_HPP
#define EX2_THREADSCOLLECTIONMANAGER_HPP


#include <map>
#include "Thread.hpp"
#include <list>
#include <set>
#include <algorithm>
#include <iostream>
#include <iterator>


#define FAILURE -1

#define SUCCESS 0


/**
 * A manager for existing threads and their status.
 */
class ThreadsCollectionManager {

private:
    int curr_thread_id;

    std::map<int, Thread> threads;

    std::list<int> readyQueue;

    std::set<int> waiting_fot_mutex;

    std::set<int> available_ids;

    std::set<int> blocked;

    size_t stack_size;

public:
    /**
     * Constructor for initializing the collection manager.
     * @param max_threads The maximal number of threads
     * that the library can manage.
     * @param stack_size The memory block size for each thread's stack.
     */
    explicit ThreadsCollectionManager(int max_threads, std::size_t stack_size)
        : curr_thread_id(0), stack_size(stack_size){
        for (int i = 1; i < max_threads; i++){
            available_ids.insert(i);
        }
        threads[curr_thread_id] = Thread();
    }

    /**
     * Create a new thread and add it to the collection and to the ready queue.
     * @param entryPoint A pointer to the function which will be the entry point of the thread.
     * @return the new thread's id upon success and -1 on failure.
     */
    int create_thread(EntryPoint entryPoint){
        if (available_ids.empty()){
            return FAILURE;
        }
        int new_id = *available_ids.begin();
        available_ids.erase(available_ids.begin());
        threads[new_id] = Thread(new_id, stack_size, entryPoint);
        readyQueue.push_back(new_id);
        return new_id;
    }


    /**
     * @param id
     * @return true iff a thread with id exists.
     */
    bool contains(int id){ return threads.count(id) > 0; }


    /**
     * Terminate the given thread from every relevant structure
     * @param id
     */
    void terminate(int id){
        threads.erase(id);
        readyQueue.remove(id);
        waiting_fot_mutex.erase(id);
        blocked.erase(id);
        available_ids.insert(id);
    }


    /**
     * Set thread's status as ready and add it to the waiting threads.
     * @param id
     */
    void set_as_ready(int id){
        if (curr_thread_id != id && std::find(readyQueue.begin(), readyQueue.end(), id) == readyQueue.end()
            && waiting_fot_mutex.count(id) ==0 && blocked.count(id) == 0){
            readyQueue.push_back(id);
        }
    }


    /**
     * @return id of running thread.
     */
    int get_curr_id() const { return curr_thread_id; }


    /**
     * Add thread to the waiting for mutex list.
     * @param id
     */
    void wait_for_mutex(int id){ waiting_fot_mutex.insert(id); }


    /**
     * Release a thread which is waiting for the mutex and add it to the
     * ready list.
     */
    void advance_mutex_line(){
        if (waiting_fot_mutex.empty()){
            return;
        }
        std::set<int> waiting_not_blocked;
        std::set_difference(waiting_fot_mutex.begin(), waiting_fot_mutex.end(), blocked.begin(),
                            blocked.end(), std::inserter(waiting_not_blocked, waiting_not_blocked.end()));
        if (!waiting_not_blocked.empty()){
            int id = *waiting_not_blocked.begin();
            readyQueue.push_back(id);
            waiting_fot_mutex.erase(id);
            return;
        }
        waiting_fot_mutex.erase(waiting_fot_mutex.begin());
    }


    /**
     * Resume a blocked thread
     * @param id
     * @return 0 upon success and -1 on failure.
     */
    int resume(int id){
        if (!contains(id)){
            return FAILURE;
        }
        blocked.erase(id);
        set_as_ready(id);
        return SUCCESS;
    }


    /**
     * Pop front of ready queue and change it to running
     */
    void set_next_thread_as_running(){
        int id_next = readyQueue.front();
        readyQueue.pop_front();
        curr_thread_id = id_next;
    }


    /**
     * @return The running thread.
     */
    Thread& get_current_thread() { return threads[curr_thread_id]; }


    /**
     * @param id
     * @return Return a reference to the thread with the given id.
     */
    Thread& get_thread(int id) { return threads[id];}
    bool is_someone_waiting(){
        return !readyQueue.empty();
    }

    /**
     * Block the thread with the given id.
     * @param id
     */
    void block(int id){
        blocked.insert(id);
        readyQueue.remove(id);
    }
};


#endif //EX2_THREADSCOLLECTIONMANAGER_HPP
