#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define thread_stacksize 50000
#define mailbox_size 20

struct thread {
  char name[20];
  void **stack[thread_stacksize];
  volatile long *stackptr;
  volatile struct thread *next;
};

// globally allocate register r15 for current thread
volatile register struct thread *current_thread asm ("%r15");

void
print_current_thread_state()
{
  volatile long *sp = current_thread->stackptr;
  printf("[%s] SP=%lx [R14=%lx R13=%lx R12=%lx RBX=%lx RBP=%lx IP=%lx]\n",
         current_thread->name,
         (long)sp, sp[0], sp[1], sp[2], sp[3], sp[4], sp[5]);
}


// yield code hidden behind a function pointer, C compiler can't inline that.

#define yield()  (yield_func())
void (*yield_func)() = 0;


volatile int one = 1;

void
yield___()
{
  // init code to 'export' the yield_entry label from this function
  asm volatile("lea yield_entry, %rdx");
  asm volatile("mov %%rdx, %0" : "=m"(yield_func));
  if (one)
    return;

  // save all callee-saved registers
  // (r15 is callee-saves too, but globally allocated for current_thread)
  asm volatile ("yield_entry:");
  asm("push %rbp");
  asm("push %rbx");
  asm("push %r12");
  asm("push %r13");
  asm("push %r14");

  // save stack pointer
  asm("mov %%rsp, %0" : "=r"(current_thread->stackptr));

#ifdef YIELD_VERBOSE
    printf("leave ");
    print_current_thread_state();
#endif

  current_thread = current_thread->next;

#ifdef YIELD_VERBOSE
    printf("enter ");
    print_current_thread_state();
#endif

  // fetch new stack pointer
  asm("mov %0, %%rsp" : : "r"(current_thread->stackptr));

  asm("pop %r14");
  asm("pop %r13");
  asm("pop %r12");
  asm("pop %rbx");
  asm("pop %rbp");

  asm("retq");
}


typedef void (*thread_entry_function)();

void
thread_push(struct thread *th, long value)
{
  *--th->stackptr = value;
}

// create a new thread and link it into the list of threads after the current thread
struct thread*
spawn(char *name, thread_entry_function entry)
{
  struct thread *th = (struct thread*)malloc(sizeof(struct thread));
  if (name)
    {
      strncpy(th->name, name, sizeof(th->name));
      th->name[sizeof(th->name)-1] = 0;
    }
  th->stackptr = (void*)(th->stack + thread_stacksize-10);
  printf("allocated thread %s at %lx (initial sp = %lx)\n",
         name, (long)th, (long)th->stackptr);

  thread_push(th, 0xbad07);
  thread_push(th, 0xbad08);
  thread_push(th, 0xbad09);
  thread_push(th, 0xbad0a);

  thread_push(th, (long)entry); // yield()'s return will jump there
  thread_push(th, 0x1116); // initial values for five of the six callee-saved registers
  thread_push(th, 0x1111); // (r15 is globally allocated)
  thread_push(th, 0x1112);
  thread_push(th, 0x1113);
  thread_push(th, 0x1114);

  // schedule by linking into list of threads
  volatile struct thread *ctnext = current_thread->next;
  current_thread->next = th;
  th->next = ctnext;

  return th;
}


struct thread initial_thread;

void
init()
{
  strcpy(initial_thread.name, "initial_thread");
  current_thread = &initial_thread;
  current_thread->next = current_thread;

  // this is only initializing the yield system
  yield___();
}


struct mailbox {
  volatile int writeptr;
  volatile int readptr;
  volatile long values[mailbox_size];
};

void
init_mailbox(struct mailbox *m)
{
  m->writeptr = 0;
  m->readptr = 0;
}

// send message to mailbox or block till space free
void
send(struct mailbox *m, long value)
{
  int w;
  while (1)
    {
      w = m->writeptr + 1;
      if (w >= mailbox_size)
        w -= mailbox_size;
      // check for space free in mailbox
      if (w != m->readptr)
        {
          m->writeptr = w;
          m->values[w] = value;
          return;
        }
      // else block
      yield();
    }
}

long
receive(struct mailbox *m)
{
  while (m->writeptr == m->readptr)
    yield();
  int p = m->readptr + 1;

  if (p >= mailbox_size)
    p -= mailbox_size;

  m->readptr = p;
  return m->values[p];
}

struct mailbox fib;

// produce infinite list of fibonaccies (modulo 2**64)
void
fibonacci()
{
  long a = 1, b = 1;
  while (1)
    {
      // printf("sending %li\n", a);
      send(&fib, a);
      long t = a;
      a += b;
      b = t;
    }
}


#ifdef BENCH
int num_fibs = 177*1000*1000;
#endif


void
print_fibonacci()
{
  while (1)
    {
      long next = receive(&fib);
#ifdef BENCH
      if (num_fibs-- == 0)
        exit(0);
#else
      printf("next fib: %li\n", next);
#endif

    }
}

void
create_initial_thread()
{
  init_mailbox(&fib);

  struct thread *f = spawn("fibonacci", fibonacci);
  struct thread *p = spawn("print_fibonacci", print_fibonacci);

  // circular list of two processes:
  f->next = p;
  p->next = f;

  yield();
}


int
main()
{
  init();
  create_initial_thread();
  return 0;
}
