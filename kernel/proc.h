// Per-CPU state
struct cpu {
	uchar apicid;                // Local APIC ID
	struct context *scheduler;   // swtch() here to enter scheduler
	struct taskstate ts;         // Used by x86 to find stack for interrupt
	segdesc gdt[NSEGS];          // x86 global descriptor table
	volatile uint started;       // Has the CPU started?
	int ncli;                    // Depth of pushcli nesting.
	int intena;                  // Were interrupts enabled before pushcli?
	struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
	uint edi;
	uint esi;
	uint ebx;
	uint ebp;
	uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
	// Svaki program pocinje od 0 i ide do neke granice
	// Koja se naziva program break i koja je definisana poljem sz
	uint sz;                     // Size of process memory (bytes)
	// Da bi znali gde se nalazi ta memorija koristimo page tablove
	// Slicna prica kao kod addrs u inode-ovima
	// Sluze da pretvore virtualne adrese u fizicke
	pde_t* pgdir;                // Page table
	char *kstack;                // Bottom of kernel stack for this process
	// Stanje procesa
	enum procstate state;        // Process state
	// ID procesa, svaki proces ga ima (komanda ps u konzoli npr)
	// Pomocu ovoga mozemo da ih identifikujemo
	int pid;                     // Process ID
	// Svaki proces ima svojeg roditelja
	// Procesi formiraju stablo, u kojem imamo root procesa
	// I svaka tacka u stablu, ima pokazivac na parenta
	//  * Ako imamo proces koji nema nijedno dete ono (npr ls)
	//  * On ce imati pokazivac na shell kao parent
	//  * Shell ce imati pokazivac na init
	//  * Init je koren stabla
	// Ovo je korisno jer hocemo da pokrenemo neki proces
	// I da cekamo da se on zavrsi
	// Obavestimo parente
	struct proc *parent;         // Parent process
	// Trap frame je na kernelskom stacku
	// Trap frame je za userspace
	struct trapframe *tf;        // Trap frame for current syscall
	// Kontekst je stanje izvrsavanja u kernelu
	// Za razliku od trapframe 
	// To su ustvari sacuvani registri (edi, esi, ebx, ebp, eip)
	struct context *context;     // swtch() here to run process
	// Ovo je kanal
	void *chan;                  // If non-zero, sleeping on chan
	// Provera se uglavnom da li je proces ubijen
	int killed;                  // If non-zero, have been killed
	// Niz otvorenih fajlova
	struct file *ofile[NOFILE];  // Open files
	// Radni direkturijum
	struct inode *cwd;           // Current directory
	char name[16];               // Process name (debugging)
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
