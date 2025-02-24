#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// written by SeungJaeOh
int procnicetoweight[MAXNICE - MINNICE + 1] =
    {/* 0*/ 88761, 71755, 56483, 46273, 36291,
     /* 5*/ 29154, 23254, 18705, 14949, 11916,
     /*10*/ 9548, 7620, 6100, 4904, 3906,
     /*15*/ 3121, 2501, 1991, 1586, 1277,
     /*20*/ 1024, 820, 655, 526, 423,
     /*25*/ 335, 272, 215, 172, 137,
     /*30*/ 110, 87, 70, 56, 45,
     /*35*/ 36, 29, 23, 18, 15};

struct mmap_area mmap_area_array[NMMAPAREA];
void init_map_area_array()
{
  for (int i = 0; i < NMMAPAREA; i++)
  {
    mmap_area_array[i].f = 0;
    mmap_area_array[i].addr = 0;
    mmap_area_array[i].length = 0;
    mmap_area_array[i].prot = 0;
    mmap_area_array[i].flags = 0;
    mmap_area_array[i].offset = 0;
  }
}

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // written by SeungJaeOh
  p->nice = DEFAULTNICE;
  p->runtime = 0;
  p->vruntime = 0;
  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  void init_map_area_array();
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  // copy mmap_area information into child process from parent process
  int copy_mmap_area = 0;
  for (int i = 0; i < NMMAPAREA && copy_mmap_area == 0; ++i)
  {
    if (mmap_area_array[i].p == curproc)
    {
      for (int j = 0; j < NMMAPAREA; ++j)
      {
        if (j == i)
        {
          continue;
        }
        if (mmap_area_array[j].p == 0)
        {
          mmap_area_array[j].f = mmap_area_array[i].f;
          mmap_area_array[j].addr = mmap_area_array[i].addr;
          mmap_area_array[j].length = mmap_area_array[i].length;
          mmap_area_array[j].offset = mmap_area_array[i].offset;
          mmap_area_array[j].prot = mmap_area_array[i].prot;
          mmap_area_array[j].flags = mmap_area_array[i].flags;
          mmap_area_array[j].p = np;
          copy_mmap_area = 1;
          break;
        }
      }
    }
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // written by SeungJaeOh
  np->nice = curproc->nice;
  np->runtime = curproc->runtime;
  np->vruntime = curproc->vruntime;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  const uint MILLIBIAS = 1000;
  int total_weight = 0;
  const uint SCHEDILINGLATENCY = 10 * MILLIBIAS;
  int procruntimearray[NPROC]; // if value is -1 then it is not runnable
  for (int i = 0; i < NPROC; i++)
  {
    procruntimearray[i] = -1;
  }

  for (;;)
  {
    // Enable interrupts on this processor
    sti();

    // Step 1: A task with minimum virtual runtime is scheduled

    total_weight = 0;
    acquire(&tickslock);
    const uint nowticks = MILLIBIAS * ticks;

    release(&tickslock);
    // every 10 ticks update procruntimearray to calculate runtime in SCHEDILINGLATENCY
    // normaly get total weight of all runnable process

    acquire(&ptable.lock);
    if (nowticks % SCHEDILINGLATENCY == 0)
    {
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        procruntimearray[p - ptable.proc] = -1;
        if (p->state != RUNNABLE)
          continue;
        procruntimearray[p - ptable.proc] = p->runtime;
        const int weight = procnicetoweight[p->nice];
        total_weight += weight;
      }
    }
    else
    {
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->state != RUNNABLE)
          continue;
        const int weight = procnicetoweight[p->nice];
        total_weight += weight;

        if (procruntimearray[p - ptable.proc] == -1)
        {
          procruntimearray[p - ptable.proc] = p->runtime;
        }
      }
    }

    // find a process with minimum virtual runtime
    struct proc *min_vruntime_proc = 0;
    int min_vruntime = __INT_MAX__;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
      {
        continue;
      }
      // calc virtual runtime of process in this SCHEDILINGLATENCY

      if (min_vruntime_proc == 0 || p->vruntime < min_vruntime)
      {
        min_vruntime_proc = p;
        min_vruntime = p->vruntime;
      }
    }

    if (min_vruntime_proc == 0)
    {
      release(&ptable.lock);
      continue;
    }
    // Step 2: Scheduled task gets time slice proportionla to its{weight/ total weight}

    int timeslice = SCHEDILINGLATENCY * procnicetoweight[min_vruntime_proc->nice] / total_weight;

    // if proc runtime in this Scheduling latency is bigger than timeslice, then it is not runnable
    if (min_vruntime_proc->runtime - procruntimearray[min_vruntime_proc - ptable.proc] >= timeslice)
    {
      // cprintf("proc %d is not runnable\n", min_vruntime_proc->pid);
      release(&ptable.lock);
      continue;
    }

    // Step 3: While the task is running, virtual runtime is updated

    c->proc = min_vruntime_proc;
    switchuvm(min_vruntime_proc);

    min_vruntime_proc->state = RUNNING;

    swtch(&(c->scheduler), min_vruntime_proc->context);
    switchkvm();

    c->proc = 0;

    acquire(&tickslock);
    const uint endticks = MILLIBIAS * ticks;
    release(&tickslock);

    const uint ticksdiff = endticks - nowticks;
    min_vruntime_proc->runtime += ticksdiff;
    min_vruntime_proc->vruntime += ticksdiff * procnicetoweight[20] / procnicetoweight[min_vruntime_proc->nice];

    // cprintf("min_vruntime_proc->runtime: %d\n", min_vruntime_proc->runtime);
    // cprintf("min_vruntime_proc->vruntime: %d\n", min_vruntime_proc->vruntime);
    // Step 4: After task run more than time slice, go Back to Step 1

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// written by SeungJaeOh PA01
int getnice(int pid)
{
  struct proc *p;

  int return_value = -1;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid != pid)
    {
      continue;
    }
    return_value = p->nice;
    break;
  }
  return return_value;
}

// written by SeungJaeOh PA01
int setnice(int pid, int value)
{
  struct proc *p;
  int return_value = -1;
  // if value is not valid, return -1
  if (value < MINNICE || value > MAXNICE)
  {
    return return_value;
  }

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid != pid)
    {
      continue;
    }
    p->nice = value;
    return_value = 0;
    break;
  }
  return return_value;
}

// written by SeungJaeOh PA01
// print process's name, pid, state, priority
// If the pid is 0, print out all processes'information
// Otherwise, print out corresponding process's information.
// If there is no process corresponding to the pid, print out nothing
void ps(int pid)
{
  const int MILLIBIAS = 1000;
  static char *states[] = {
      [UNUSED] "UNUSED  ",
      [EMBRYO] "EMBRYO  ",
      [SLEEPING] "SLEEPING",
      [RUNNABLE] "RUNNABLE",
      [RUNNING] "RUNNING ",
      [ZOMBIE] "ZOMBIE  "};

  struct proc *p;
  char *state;
  int first_output = 1;

  uint xticks; // to get ticks
  // if pid==0 print all processes' information and pid!=0 print corresponding process's information

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED || (pid != 0 && p->pid != pid))
    {
      continue;
    }

    if (first_output)
    {
      acquire(&tickslock);
      xticks = ticks;
      release(&tickslock);
      cprintf("name\tpid\tstate\t\tpriority\truntime/weight\truntime\tvruntime\ttick%d\n", MILLIBIAS * xticks);
      first_output = 0;
    }

    state = states[p->state];
    cprintf("%s\t%d\t%s\t%d\t\t%d\t\t%d\t%d\n", p->name, p->pid, state, p->nice, p->runtime / procnicetoweight[p->nice], p->runtime, p->vruntime);
    if (pid != 0)
    {
      break;
    }
  }
}

// If succeed, return the start address of mapping area, If failed, return 0
// If MAP_ANONYMOUS is given, it is anonyous mapping
// If MAP_ANONYMOUS is not given, it is file mapping
// If MAP_POPULATE is given, allocate physical page & make page table for whole mapping area.
// If MAP_POPULATE is not given, just record its mapping area.
uint mmap(uint addr, int length, int prot, int flags, int fd, int offset)
{
  if (addr % PGSIZE != 0 || length <= 0 || length % PGSIZE != 0)
  {
    goto bad;
  }
  struct proc *curproc = myproc();

  // Describe failed situation
  // It's not anonumous, but when the fd is -1
  // The protection of the file and the prot of the parameter are different
  if ((flags & MAP_ANONYMOUS) == 0 && fd == -1)
  {
    goto bad;
  }
  // if flags have MAP_POPULATE, allocate physical page & make page table for whole mapping area.
  struct file *f = 0;
  if ((flags & MAP_ANONYMOUS) == 0)
  {
    f = curproc->ofile[fd];
  }
  if (flags & MAP_POPULATE)
  {
    char *mem;
    char buf[PGSIZE];
    for (int i = addr + MMAPBASE; i < addr + length + MMAPBASE; i += PGSIZE)
    {

      // if failed to allocate physical page, return 0
      // 일단 고려하지 않는다.
      if ((mem = kalloc()) == 0)
      {
        goto bad;
      }

      // set memory as 0
      if (flags & MAP_ANONYMOUS)
      {
        memset(mem, 0, PGSIZE);
      }
      // set memory from fd
      else
      {
        if (f == 0)
        {
          goto bad;
        }
        while (offset > 0)
        {
          if (offset >= PGSIZE)
          {
            fileread(f, buf, PGSIZE);
            offset -= PGSIZE;
          }
          else
          {
            fileread(f, buf, offset);
            offset = 0;
          }
        }
        int n;
        n = fileread(f, buf, PGSIZE);
        memmove(mem, buf, n);
      }
      mappages(curproc->pgdir, (void *)i, PGSIZE, V2P(mem), PTE_W | PTE_U);
    }
  }

  // store mmap_area information into mmap_area_array
  for (int i = 0; i < NMMAPAREA; ++i)
  {
    if (mmap_area_array[i].p == 0)
    {
      mmap_area_array[i].f = f;
      mmap_area_array[i].addr = addr + MMAPBASE;
      mmap_area_array[i].length = length;
      mmap_area_array[i].offset = offset;
      mmap_area_array[i].prot = prot;
      mmap_area_array[i].flags = flags;
      mmap_area_array[i].p = curproc;
      return addr + MMAPBASE;
    }
  }

bad:
  // dealloc memory table
  return 0;
}

int munmap(uint addr)
{
  struct proc *curproc = myproc();
  for (int i = 0; i < NMMAPAREA; ++i)
  {
    if (mmap_area_array[i].p == curproc && mmap_area_array[i].addr == addr)
    {
      pte_t *pte;
      uint pa;

      for (int j = addr; j < addr + mmap_area_array[i].length; j += PGSIZE)
      {
        pte = walkpgdir(curproc->pgdir, (const void *)j, 0);
        if (pte == 0)
        {
          continue;
        }
        pa = PTE_ADDR(*pte);
        char *v = P2V(pa);
        kfree(v);
      }

      mmap_area_array[i].f = 0;
      mmap_area_array[i].addr = 0;
      mmap_area_array[i].length = 0;
      mmap_area_array[i].offset = 0;
      mmap_area_array[i].prot = 0;
      mmap_area_array[i].flags = 0;
      mmap_area_array[i].p = 0;
      return 1;
    }
  }
  return -1;
}

extern int free_page_cnt;

int freemem()
{
  return free_page_cnt;
}