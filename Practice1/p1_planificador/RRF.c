#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];

/* Current running thread */
static TCB* running;
static int current = 0;

// Creates an empty queue
static struct queue * q;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {
  int i;
  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if(idle.run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1);

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;

  running = &t_state[0];
  printf("*** THREAD READY : SET CONTEXT TO %d\n", running->tid);

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();

  q = queue_new();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;

  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].ticks = QUANTUM_TICKS;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1);


  printf("*** THREAD %d READY\n", t_state[i].tid);

  disable_interrupt ();
  if (running != &t_state[i] && mythread_getpriority()==LOW_PRIORITY && priority==HIGH_PRIORITY) {
      TCB* aux = running;
      printf("*** THREAD %d PREEMTED : SETCONTEXT OF %d\n", running->tid, t_state[i].tid);
      running = &t_state[i];
      current = running->tid;
      enqueue (q , aux);
      swapcontext (&(aux->run_env), &(running->run_env));
  } else if (priority==LOW_PRIORITY){
      printf("hhh THREAD %d ARRIVED : CURRENTLY RUNNING %d SO QUEUEING IT\n", t_state[i].tid,  running->tid);
      // Take the element from the TCB table
      TCB * t = & t_state [i];
      // insert a TCB into the queue
      enqueue (q , t);
  }
  enable_interrupt ();
  return i;
} /****** End my_thread_create() ******/

/* Read disk syscall */
int read_disk()
{
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{
}


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();

  printf("*** THREAD %d FINISHED\n", tid);
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  TCB* next = scheduler();
  activator(next);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority() {
  int tid = mythread_gettid();
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}


/* FIFO para alta prioridad, RR para baja*/
TCB* scheduler(){
    disable_interrupt ();
  int i;
  for(i=0; i<N; i++){
    if (t_state[i].state == INIT && t_state[i].priority == HIGH_PRIORITY) {
        current = i;
        printf("hhh NEXT THREAD %d PRIORITY HIGH\n", t_state[i].tid);
        enable_interrupt();
	    return &t_state[i];
    }
  }
  if (!queue_empty(q)) {
      TCB * candidate = dequeue ( q ) ;
      while (candidate->state != INIT) {
          enqueue (q , candidate);
          candidate = dequeue ( q ) ;
      }
      current = candidate->tid;
      printf("hhh NEXT THREAD %d PRIORITY LOW\n", candidate->tid);
      enable_interrupt();
      return candidate;
  }
  printf("mythread_free: No thread in the system\nExiting...\n");
  printf("*** FINISH\n");
  exit(1);
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{

    disable_interrupt ();
    if (mythread_getpriority() == LOW_PRIORITY) {
        running->ticks--;
        printf("hhh THREAD %d - TICKS %d\n", running->tid, running->ticks);
        if (running->ticks<=0){
            running->ticks = QUANTUM_TICKS;
            running->state = INIT;
            printf("hhh TICKS FINISHED. STORING THREAD %d IN QUEUE\n", running->tid);
            enqueue (q , running);
            TCB* next = scheduler();
            if (next != running) {
                TCB* aux = running;
                printf("*** SWAPCONTEXT FROM %d TO %d\n", running->tid, next->tid);
                running = next;
                current = running->tid;
                swapcontext (&(aux->run_env), &(running->run_env));
            }
        }
    }
    enable_interrupt();
}

/* Activator */
void activator(TCB* next){
    TCB * aux = running;
    running = next;
    printf("*** THREAD %d TERMINATED : SETCONTEXT OF %d\n", aux->tid, running->tid);
    setcontext (&(next->run_env));
    printf("mythread_free: After setcontext, should never get here!!...\n");
}