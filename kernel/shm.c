#include "types.h"
#include "defs.h"
#include "fcntl.h"
#include "mmu.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "shm.h"
#include "proc.h"

#define FF(f) (f & O_RDWR ? (PTE_U|PTE_W) : PTE_U)

struct {
    struct spinlock lock;
    struct shmo shm[NSHMO];
} shmtable;

// Creates the lock for shm objects
void
shminit(void)
{
	initlock(&shmtable.lock, "shmtable");
}

// Finds an unused shm object in the table of objects
// And return the reference
static struct shmo*
shmalloc(void)
{
    struct shmo* s;

    acquire(&shmtable.lock);
    for (s = shmtable.shm; s < shmtable.shm + NSHMO; s++){
        if (s->ref==0) {
            s->ref=1;
            release(&shmtable.lock);
            return s;
        }
    }

    release(&shmtable.lock);
    return 0;
}

// Increases the ref count of shm object
struct shmo*
shmdup(struct shmo* s) {
    acquire(&shmtable.lock);
    if (s->ref < 1)
        panic("shmdup");
    s->ref++;
    release(&shmtable.lock);
    return s;
}

// Tries to find shm object with given name
// In the running proc or returns -1 if not found
static int
fproc(char* name)
{
    struct proc* curproc = myproc();
    struct shmo* s;

    acquire(&shmtable.lock);
    int i;
    for (i = 0; i < NOSHMO; i++) {
        s = curproc->oshmo[i];
        if (strncmp(name, s->name, strlen(name)) == 0) {
            release(&shmtable.lock);
            return i;
        }
    }

    release(&shmtable.lock);
    return -1;
}

// Tries to find shm object with given name
// Returns a reference to it
// Increases the ref cnt, or 0 if it doesn't exist
static struct shmo*
fmem(char* name)
{
    struct shmo* s;

    acquire(&shmtable.lock);
    for (s = shmtable.shm; s < shmtable.shm + NSHMO; s++){
        if (strncmp(s->name, name, strlen(name)) == 0) {
            s->ref++;
            release(&shmtable.lock);
            return s;
        }
    }

    release(&shmtable.lock);
    return 0;
}

// Close shm object.  (Decrement ref count, close when reaches 0.)
void
shmclose(struct shmo *s)
{
	acquire(&shmtable.lock);
	if (s->ref < 1)
		panic("shmclose");
	if (--s->ref > 0) {
		release(&shmtable.lock);
		return;
	}

    //cprintf("No references here, we should clear the pages\n");
    for (int i = 0; i < NPAGES; i++) {
        if (s->pgs[i]) {
            kfree(s->pgs[i]);
        }
        s->pgs[i] = 0;
    }

	s->ref = 0;
    s->size = 0;
    memset(s->name, 0, strlen(s->name));
    
	release(&shmtable.lock);
}

// Allocate a shm object descriptor for the given shm object.
static int
odalloc(struct shmo *s)
{
	struct proc *curproc = myproc();
    acquire(&shmtable.lock);

    // Looks for the shm object in the process first
	for(int od = 0; od < NOSHMO; od++){
        if (curproc->oshmo[od] == s) {
            release(&shmtable.lock);
            return od;
        }
	}

    // If not in the process, find an empty space
    for(int od = 0; od < NOSHMO; od++){
        if(curproc->oshmo[od] == 0){
			curproc->oshmo[od] = s;
            release(&shmtable.lock);
			return od;
		}
	}

    // We've reached the limit
    release(&shmtable.lock);
	return -1;
}

int 
shm_open(char* name) {
    struct shmo* sh;
    int od;

    // Try to find the object with the name
    // In the current process
    if ((od=fproc(name))!=-1) {
        return od;
    }

    // Try to find it in the table of processes
    if ((sh=fmem(name)) != 0 && (od=odalloc(sh)) != -1) {
        return od;
    }

    if ((sh=shmalloc()) == 0 || (od=odalloc(sh)) < 0) {
        if (sh) { // In case we allocated shm object but theres no more space in the process, deallocate it
            shmclose(sh);
        }
        return -1;
    }

    strncpy(sh->name, name, strlen(name));
    return od;
}

int
shm_trunc(int od, int sz) {
    struct proc *curproc = myproc();
    struct shmo *s;

    if (od < 0 || od >= NOSHMO) {
        return -1;
    }

    acquire(&shmtable.lock);
    if ((s = curproc->oshmo[od]) == 0) {
        release(&shmtable.lock);
        return -1;
    }

    if (s->size != 0) { // Size is already set
        release(&shmtable.lock);
        return -1;
    }
    
    if (sz < 0 || sz > (32*PGSIZE)) { // sz is invalid
        release(&shmtable.lock);
        return -1;
    }

    // Try to allocate pages for this shm object
    char* mem;
    for (int i = 0; i < PGS(sz); i++) {
        if ((mem=kalloc()) == 0) {
            // Cleanup data if one failed?
            release(&shmtable.lock);
            return -1;
        }
        
        s->pgs[i] = (char*) mem;        
        memset(s->pgs[i], 0, PGSIZE); // Fill the page with zeros
    }

    s->size = sz;
    release(&shmtable.lock);
    return sz;
}

unsigned int curr = KERNBASE/2;

unsigned int get_addr() {
    unsigned int res = curr;
    curr += 4096 * 8;
    if (curr >= KERNBASE) {
        curr = 0;
    }

    return res;
}

int
shm_map(int od, void **va, int flags) {
    acquire(&shmtable.lock);

    struct proc *curproc = myproc();
    struct shmo *s;

    if ((s = curproc->oshmo[od]) == 0) {
        release(&shmtable.lock);
        return -1;
    }
    
    void* start = ADDR(curproc); // Start virtual address
    void* current = start; // Current virtual address
    for (int i = 0; i < PGS(s->size); i++) {
        //cprintf("%d\n", curproc->lva);
        if (mappages(curproc->pgdir, current, PGSIZE, (uint) V2P(s->pgs[i]), FF(flags)) < 0) {
            kfree(s->pgs[i]);
            release(&shmtable.lock);
            return -1;
        }

        current += PGSIZE;
        curproc->lva += PGSIZE;
    }

    curproc->vpgs[od] = start;
    *va = start;
    release(&shmtable.lock); 
    return 0;
}

int
shm_close(int od) {
    acquire(&shmtable.lock);
    struct proc *curproc = myproc();
    struct shmo *s;

    if ((s=curproc->oshmo[od]) == 0) {
        return -1;
    }

    //cprintf("The virtual address of this proc is %d\n", curproc->vpgs[od]);
    if (curproc->vpgs[od]) {
        //cprintf("Should clear\n");
        unmap(curproc->pgdir, curproc->vpgs[od]);
        //memset(curproc->vpgs[od], 0, s->size);
    }

    release(&shmtable.lock);
    shmclose(s);

    acquire(&shmtable.lock);

    curproc->vpgs[od] = 0;
    curproc->oshmo[od] = 0;

    release(&shmtable.lock);
    return 1;
}

void unmap(pde_t *pgdir, void *va) {
    pte_t *pte;

    pte = walkpgdir(pgdir, va, 0);
    if (pte != 0) {
        //cprintf("The mapping is for %d\n", *pte);
        *pte = 0; // Postavite PTE na nulu
        //cprintf("The mapping is for %d\n", *pte);
        //cprintf("Stavljamo pte na nulu\n");
    }
}
