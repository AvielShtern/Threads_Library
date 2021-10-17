#ifndef EX2_THREAD_HPP
#define EX2_THREAD_HPP


#include <csignal>
#include <csetjmp>
#include <unistd.h>
#include <cstddef>
#include "uthreads.h"
#include <iostream>
#include <memory>



#define SYS_ERROR_MSG "system error: "
#define ERR_SIG "Error in signal handling."
#define JB_SP 6
#define JB_PC 7

using std::cerr;
using std::endl;
using std::size_t;


typedef unsigned long address_t;
typedef void (*EntryPoint)(void);



/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

/**
 * One thread with an env.
 */
class Thread{
public:
    int id;
    sigjmp_buf env;
    std::shared_ptr<char> stack;
    size_t quantums;

    /**
     * Constructor for a thread (except the main one).
     * @param id
     * @param stack_size
     * @param entry_point Entry point of the thread
     */
    Thread(int id, size_t stack_size,  EntryPoint entry_point)
        : id(id), env{0}, quantums(0){
          stack = std::shared_ptr<char>(new char[STACK_SIZE]);
        address_t sp = (address_t)stack.get() + stack_size - sizeof(address_t);
        auto pc = (address_t)entry_point;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        if (sigemptyset(&env->__saved_mask) < 0){
            cerr << SYS_ERROR_MSG << SIG_ERR << endl;
            std::exit(EXIT_FAILURE);
        }
    }

    /**
     * Constructor for a thread without allocating stack (main thread).
     */
    explicit Thread(): id(0), env{0}, stack(nullptr), quantums(1) {}

};


#endif //EX2_THREAD_HPP
