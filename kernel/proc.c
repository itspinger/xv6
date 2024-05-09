#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
	struct spinlock lock;
	struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
	initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid()
{
	return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
	int apicid, i;

	if(readeflags()&FL_IF)
		panic("mycpu called with interrupts enabled\n");

	apicid = lapicid();
	// APIC IDs are not guaranteed to be contiguous. Maybe we should have
	// a reverse map, or reserve a register to store &cpus[i].
	for (i = 0; i < ncpu; ++i) {
		if (cpus[i].apicid == apicid)
			return &cpus[i];
	}
	panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
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

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
//
// Trazi neiskorisceni proces
// I inicijalizuje ga da bi mogao da se izvrsi na kernelu
static struct proc*
allocproc(void)
{
	struct proc *p;
	char *sp;

	acquire(&ptable.lock);

	// Trazimo taj proces
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if(p->state == UNUSED)
			goto found;

	release(&ptable.lock);
	return 0;

found:
	// Nasli smo proces
	p->state = EMBRYO;
	p->pid = nextpid++;

	release(&ptable.lock);

	// Allocate kernel stack.
	// Alociramo kernelski stack
	if((p->kstack = kalloc()) == 0){
		// Ako se desi greska stavimo ga kao nekoriscenog
		// I vracamo u tabelu
		// Da moze neko drugi da iskoristi
		p->state = UNUSED;
		return 0;
	}
	// Stavljamo da stack pointer bude kstack + KSTACKSIZE
	// KSTACKSIZE je jedna stranica u memoriji??
	// kalloc() takodje vraca jednu stranicu memorije
	// Na kraj te stranice stavljamo pokazivac sp
	// Zato sto stack raste na dole
	sp = p->kstack + KSTACKSIZE;

	// Leave room for trap frame.
	// Dodajemo trap frame na stack pointer
	// Tj smanjujemo sp za velicinu trapframa
	sp -= sizeof *p->tf;
	p->tf = (struct trapframe*)sp; // Uzimamo trapframe, pretvaramo ga u sp
	// I cuvamo u p->tf strukture proc

	// Set up new context to start executing at forkret,
	// which returns to trapret.
	// Trap ret je funkcija koja ce da ucita trapframe
	// I kraj trep funkcije
	sp -= 4;
	*(uint*)sp = (uint)trapret; // Koristimo trapret
	// To je deo trepa
	// Koji ce da ucita celo stanje iz trapframe-a
	// I vracamo se na userspace

	// Nakon svega toga, stavljamo kontekst
	// Zato sto hocemo da zapisemo gde skacemo
	// Da bismo nastavili izvrsavanje
	sp -= sizeof *p->context;
	p->context = (struct context*)sp;
	memset(p->context, 0, sizeof *p->context);
	// Po defaultu, skacemo na
	// Forkret je bukvalno prazna funkcija
	p->context->eip = (uint)forkret;

	return p;
}

// Set up first user process.
// Koren stabla nastaje u ovoj funkciji
void
userinit(void)
{
	struct proc *p;
	extern char _binary_user_initcode_start[], _binary_user_initcode_size[];

	// Zove se alloc
	// Tj alociramo novi proces
	p = allocproc();

	// Sacuvacemo ga u globalnu promenljivu initproc
	initproc = p;
	// Postavlja kernelski deo stranicne tabele
	if((p->pgdir = setupkvm()) == 0)
		panic("userinit: out of memory?");
	// Inicijalizuje korisnicku virtuelnu memoriju
	inituvm(p->pgdir, _binary_user_initcode_start, (int)_binary_user_initcode_size);
	p->sz = PGSIZE;
	memset(p->tf, 0, sizeof(*p->tf));
	p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
	p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
	p->tf->es = p->tf->ds;
	p->tf->ss = p->tf->ds;
	p->tf->eflags = FL_IF;
	p->tf->esp = PGSIZE;
	p->tf->eip = 0;  // beginning of initcode.S

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
int
growproc(int n)
{
	uint sz;
	struct proc *curproc = myproc();

	sz = curproc->sz;
	if(n > 0){
		if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
			return -1;
	} else if(n < 0){
		if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
			return -1;
	}
	curproc->sz = sz;
	switchuvm(curproc);
	return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
//
// Fork pravi novi procese kopirajuci stare
// Zove se fork zato sto zapravo to kopiranje izgleda kao fork
// Postavlja mu stek tako da se vrati iz sistemskog poziva
// Stavlja mu state na runnable
int
fork(void)
{
	int i, pid;
	struct proc *np;
	struct proc *curproc = myproc();

	// Allocate process.
	// Allocproc alocira novi kernelski stek
	if((np = allocproc()) == 0){
		return -1;
	}

	// Copy process state from proc.
	// Kopiramo celu virtuelnu memoriju
	if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
		kfree(np->kstack);
		np->kstack = 0;
		np->state = UNUSED;
		return -1;
	}
	// Kopiramo velicinu starog procesa u novi
	// Stavljamo mu parent, i kopiramo trapframe (stanje procesas)
	np->sz = curproc->sz;
	np->parent = curproc;
	*np->tf = *curproc->tf;

	// Clear %eax so that fork returns 0 in the child.
	// Zbog ove linije kada zovemo fork() ono vraca 2 vrednosti
	np->tf->eax = 0;

	// Kopiramo sve fajlove koje smo otvorili
	for(i = 0; i < NOFILE; i++)
		if(curproc->ofile[i])
			// Tako sto iz dupujemo
			np->ofile[i] = filedup(curproc->ofile[i]);
	np->cwd = idup(curproc->cwd); // Kopiramo cwd

	// Kopiramo ime
	safestrcpy(np->name, curproc->name, sizeof(curproc->name));

	// Svaki proces ima svoj pid
	pid = np->pid;

	acquire(&ptable.lock);

	// Stavimo kao runnable i vratimo
	np->state = RUNNABLE;

	release(&ptable.lock);

	return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
//
// Kad se desi exit, mi zapravo zavrsavamo neki wait()
void
exit(void)
{
	struct proc *curproc = myproc();
	struct proc *p;
	int fd;

	if(curproc == initproc)
		panic("init exiting");

	// Close all open files.
	// Zatvara sve otvorene fajlove
	for(fd = 0; fd < NOFILE; fd++){
		if(curproc->ofile[fd]){
			fileclose(curproc->ofile[fd]);
			curproc->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(curproc->cwd); // Otpusti current working directory
	end_op();
	curproc->cwd = 0;

	acquire(&ptable.lock);

	// Parent might be sleeping in wait().
	// Probudi roditelja procesa
	wakeup1(curproc->parent);

	// Pass abandoned children to init.
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->parent == curproc){
			p->parent = initproc; // Stavlja parent na init
			if(p->state == ZOMBIE)
				wakeup1(initproc);
		}
	}

	// Jump into the scheduler, never to return.
	curproc->state = ZOMBIE; // Stavljamo proces na zombie
	sched(); // Tek kad uradimo sched ce onaj proces koji smo wakeupovali probuditi
	panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
	struct proc *p;
	int havekids, pid;
	struct proc *curproc = myproc();

	acquire(&ptable.lock);
	for(;;){
		// Scan through table looking for exited children.
		// Skeniramo kroz tabelu i gledamo da li imamo
		// Decu
		havekids = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->parent != curproc)
				continue;
			havekids = 1; // Imamo
			// Ako je zombie onda smo docekali da jedan od nasih
			// Podprocesa exituje
			// Kada nadjemo zombija kao dete
			// Oslobodi sve informacije o detetu
			if(p->state == ZOMBIE){
				// Found one.
				pid = p->pid;
				kfree(p->kstack);
				p->kstack = 0;
				freevm(p->pgdir);
				p->pid = 0;
				p->parent = 0;
				p->name[0] = 0;
				p->killed = 0;
				p->state = UNUSED; // Da bi posle opet mogao da se koristi
				release(&ptable.lock);
				// Ovo je uradjeno da bi roditelju bila vracena
				// Informacija o id-u detetovog procesa
				return pid;
			}
		}

		// No point waiting if we don't have any children.
		// Ako nemamo dece, ili je proces ubijen, otpusti lock
		if(!havekids || curproc->killed){
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.  (See wakeup1 call in proc_exit.)
		// U suprotnom, cekamo da neko wakeupuje nas proces
		sleep(curproc, &ptable.lock);  //DOC: wait-sleep
	}
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
//
// Rasporedjivac je najbitniji deo kernela
// Jedina stvar koja je samo za kernel
void
scheduler(void)
{
	int idle;
	struct proc *p;
	struct cpu *c = mycpu();
	c->proc = 0;

	idle = 0;
	for(;;){
		// Enable interrupts on this processor.
		sti();

		// If there are no processes to run, halt the CPU
		// until the next interrupt.
		if(idle)
			hlt();
		idle = 1;

		// Loop over process table looking for process to run.
		acquire(&ptable.lock);
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->state != RUNNABLE)
				continue;

			idle = 0;
			// Switch to chosen process.  It is the process's job
			// to release ptable.lock and then reacquire it
			// before jumping back to us.
			c->proc = p;
			switchuvm(p);
			p->state = RUNNING;

			swtch(&(c->scheduler), p->context);
			switchkvm();

			// Process is done running for now.
			// It should have changed its p->state before coming back.
			c->proc = 0;
		}
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
void
sched(void)
{
	int intena;
	struct proc *p = myproc();

	if(!holding(&ptable.lock))
		panic("sched ptable.lock");
	if(mycpu()->ncli != 1)
		panic("sched locks");
	if(p->state == RUNNING)
		panic("sched running");
	if(readeflags()&FL_IF)
		panic("sched interruptible");
	intena = mycpu()->intena;
	swtch(&p->context, mycpu()->scheduler);
	mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
// Otpustamo kontrolu procesora
void
yield(void)
{
	acquire(&ptable.lock);  //DOC: yieldlock
	// Stavljamo stanje procesora na runnable
	// I zovemo sched funkciju
	myproc()->state = RUNNABLE;
	sched();
	release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
	static int first = 1;
	// Still holding ptable.lock from scheduler.
	release(&ptable.lock);

	if (first) {
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
//
// Ceka na kanalu chan i da otpusti lock privremeno
void
sleep(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	if(p == 0)
		panic("sleep");

	if(lk == 0)
		panic("sleep without lk");

	// Must acquire ptable.lock in order to
	// change p->state and then call sched.
	// Once we hold ptable.lock, we can be
	// guaranteed that we won't miss any wakeup
	// (wakeup runs with ptable.lock locked),
	// so it's okay to release lk.
	if(lk != &ptable.lock){  //DOC: sleeplock0
		acquire(&ptable.lock);  //DOC: sleeplock1
		release(lk);
	}
	// Go to sleep.
	p->chan = chan;
	p->state = SLEEPING;

	sched(); // Aktiviramo neki drugi proces za izvrsavanje
	// Nakon sto smo stavili ovaj proces na spavanje

	// Ovde stajemo i cekamo
	// Neko zove wakeup, tj wakeup1 se zove...
	// Skida se can na liniji dole

	// Tidy up.
	p->chan = 0;

	// Reacquire original lock.
	if(lk != &ptable.lock){  //DOC: sleeplock2
		release(&ptable.lock);
		acquire(lk); // Uzima lock koji smo otpustili malopre
	}
}

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
	struct proc *p;

	// Prolazi kroz tabelu procesa
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		// Uzima sleeping proces koji ceka na kanalu
		// Koji smo prosledili
		if(p->state == SLEEPING && p->chan == chan)
			// I stavlja taj proces na runnable
			p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
	acquire(&ptable.lock);
	wakeup1(chan);
	release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
	struct proc *p;

	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			p->killed = 1;
			// Wake process from sleep if necessary.
			if(p->state == SLEEPING)
				p->state = RUNNABLE;
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
	static char *states[] = {
	[UNUSED]    "unused",
	[EMBRYO]    "embryo",
	[SLEEPING]  "sleep ",
	[RUNNABLE]  "runble",
	[RUNNING]   "run   ",
	[ZOMBIE]    "zombie"
	};
	int i;
	struct proc *p;
	char *state;
	uint pc[10];

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state == UNUSED)
			continue;
		if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
			state = states[p->state];
		else
			state = "???";
		cprintf("%d %s %s", p->pid, state, p->name);
		if(p->state == SLEEPING){
			getcallerpcs((uint*)p->context->ebp+2, pc);
			for(i=0; i<10 && pc[i] != 0; i++)
				cprintf(" %p", pc[i]);
		}
		cprintf("\n");
	}
}
