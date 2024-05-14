#include "types.h"
#include "defs.h"
#include "fcntl.h"
#include "mmu.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "shm.h"
#include "proc.h"

struct {
    struct spinlock lock;
    struct shmo shm[NSHMO];
} shmtable;

void
shminit(void)
{
	initlock(&shmtable.lock, "shmtable");
}

struct shmo*
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

    cprintf("Making\n");

    release(&shmtable.lock);
    return 0;
}

struct shmo*
shmdup(struct shmo* s) {
    acquire(&shmtable.lock);
    if (s->ref < 1)
        panic("shmdup");
    s->ref++;
    release(&shmtable.lock);
    return s;
}

int
findshms(char* name)
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

void printtable() {
    struct shmo* s;

    acquire(&shmtable.lock);
    int i = 0;
    for (s = shmtable.shm; s < shmtable.shm + NSHMO; s++) {
        if (!strlen(s->name)) {
            continue;
        }
        cprintf("For i = %d, s is %s\n", i, s->name);
        i++;
    }

    release(&shmtable.lock);
}

struct shmo*
findinmemory(char* name)
{
    struct shmo* s;

    acquire(&shmtable.lock);
    for (s = shmtable.shm; s < shmtable.shm + NSHMO; s++){
        if (strncmp(s->name, name, strlen(name)) == 0) {
            //cprintf("nasli smo!!!\n");
            s->ref++;
            release(&shmtable.lock);
            return s;
        }
    }

    release(&shmtable.lock);
    return 0;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
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

    cprintf("Clearing pages\n");

    for (int i = 0; i < NPAGES; i++) {
        if (s->pgs[i]) {
            //cprintf("should cleanup page %d\n", i);
            kfree(s->pgs[i]);
        }
        s->pgs[i] = 0;
    }

	s->ref = 0;
    s->size = 0;
    memset(s->name, 0, strlen(s->name));
    
	release(&shmtable.lock);
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
odalloc(struct shmo *s)
{
	struct proc *curproc = myproc();
    //cprintf("Zvali smo odalloc\n");

	for(int od = 0; od < NOSHMO; od++){
        if (curproc->oshmo[od] == s) {
            //cprintf("Vratili smo za prvu %d\n", od);
            return od;
        }
	}

    for(int od = 0; od < NOSHMO; od++){
        if(curproc->oshmo[od] == 0){
			curproc->oshmo[od] = s;
            //cprintf("Vratili smo za drugu %d\n", od);
			return od;
		}
	}

    //cprintf("Vratili smo -1\n");
	return -1;
}

int 
shm_open(char* name) {
    struct shmo* sh;
    int od;

    //cprintf("called\n");
    //printtable();

    // Try to find the object with the name
    // In the current process
    if ((od=findshms(name))!=-1) {
        //cprintf("found\n");
        return od;
    }

    // Try to find it in the table of processes
    if ((sh=findinmemory(name)) != 0 && (od=odalloc(sh)) != -1) {
        //cprintf("Nasli smo u memoriji\n");
        return od;
    }

    //cprintf("Stigli smo dovde\n");
    if ((sh=shmalloc()) == 0 || (od=odalloc(sh)) < 0) {
        if (sh) { // In case we allocated shm object but theres no more space in the process, deallocate it
            shmclose(sh);
        }
        return -1;
    }

    strncpy(sh->name, name, strlen(name));
    //cprintf("Od of the shm is %d\n", od);
    //cprintf("Ref count of shm is %d\n", sh->ref);
    //cprintf("Vracamo od %d\n", od);
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

    cprintf("provera za data size %d\n", s->size);
    if (s->size != 0) { // Size is already set
        cprintf("data already exists\n");
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
        //cprintf("Calling kalloc on page with index %d\n", i);
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

int
shm_map(int od, void **va, int flags) {
    struct proc *curproc = myproc();
    struct shmo *s;

    acquire(&shmtable.lock);
    if ((s = curproc->oshmo[od]) == 0) {
        release(&shmtable.lock);
        return -1;
    }

    int f = PTE_U;
    if (flags & O_RDWR) {
        f |= PTE_W;
    }

    void* cva = (void*) P2V(KERNBASE/2 + 1);
    for (int i = 0; i < PGS(s->size); i++) {
        if (mappages(curproc->pgdir, cva, PGSIZE, (uint) V2P(s->pgs[i]), PTE_U|PTE_W) < 0) {
            kfree(s->pgs[i]);
            release(&shmtable.lock);
            return -1;
        }
    }

    curproc->vpgs[od] = (void*) P2V(KERNBASE/2 + 1);
    *va = cva;
    release(&shmtable.lock); 
    return 0;
}

void unmap(pde_t *pgdir, void *va) {
    pte_t *pte;

    pte = walkpgdir(pgdir, va, 0);
    if (pte != 0) {
        *pte = 0; // Postavite PTE na nulu
        cprintf("Stavljamo pte na nulu\n");
    }
}

int
shm_close(int od) {
    struct proc *curproc = myproc();
    struct shmo *s;

    if ((s=curproc->oshmo[od]) == 0) {
        cprintf("Can't find this process\n");
        return -1;
    }

    shmclose(s);

    if (curproc->vpgs[od]) {
        cprintf("Should clear\n");
        unmap(curproc->pgdir, curproc->vpgs[od]);
    }

    acquire(&shmtable.lock);

    //if (curproc->vpgs[od]) {
        //if (!deallocuvm(curproc->pgdir, curproc->vpgs[od] + s->size, curproc->vpgs[od])) {
            //cprintf("Error deallocating memory for shared memory region\n");
            //release(&shmtable.lock);
            //return -1;
        //}
    //}

    curproc->vpgs[od] = 0;
    curproc->oshmo[od] = 0;

    release(&shmtable.lock);
    return 1;
}