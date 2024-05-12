#include "types.h"
#include "defs.h"
#include "mmu.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "shm.h"
#include "proc.h"

#define PGS(sz) ((sz + PGSIZE - 1) / PGSIZE)

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

    release(&shmtable.lock);
    return 0;
}

// Attempts to find shmo with name in the global table of processes
// IDK if we're going to use this????
struct shmo*
findshmdeprecated(char* name)
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

	s->ref = 0;
	release(&shmtable.lock);
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
odalloc(struct shmo *s)
{
	struct proc *curproc = myproc();

	for(int od = 0; od < NOSHMO; od++){
		if(curproc->oshmo[od] == 0){
			curproc->oshmo[od] = s;
			return od;
		}
	}
	return -1;
}

int 
shm_open(char* name) {
    struct shmo* sh;
    int od;

    // Probaj da nadjes deljeni objekat
    // U procesu koji se izvrsava
    if ((od=findshms(name))!=-1) {
        return od;
    }

    if ((sh=shmalloc()) == 0 || (od=odalloc(sh)) < 0) {
        if (sh) { // U slucaju da smo alocirali shmo ali da nema mesta u procesu moramo da dealociramo resurse
            shmclose(sh);
        }
        return -1;
    }

    memmove(sh->name, name, strlen(name));
    cprintf("Od je %d\n", od);
    cprintf("Ref count je %d\n", sh->ref);
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

    if (s->size != 0) { // Velicina je vec postavljena 
        release(&shmtable.lock);
        return -1;
    }
    
    if (sz < 0 || sz > (32*PGSIZE)) { // sz je nevalidan
        release(&shmtable.lock);
        return -1;
    }

    // Probacemo da alociramo stranice za ovaj shm
    for (int i = 0; i < PGS(sz); i++) {
        cprintf("Zovemo calloc\n");
        if ((s->pgs[i]=kalloc()) == 0) {
            // Treba da ocistimo podatke ovde ako se desila greska?
            release(&shmtable.lock);
            return -1;
        }
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

    void* cva = (void*) (KERNBASE/2);
    cprintf("Pocetak je %d\n", cva);
    for (int i = 0; i < PGS(s->size); i++) {
        void* rva = cva + i * PGSIZE;
        cprintf("Vrednost V2P(s->pgs[i]) je %d\n", V2P(s->pgs[i]));
        cprintf("Vrednost P2V(s->pgs[i]) je %d\n", P2V(s->pgs[i]));
        cprintf("Vrednost s->pgs[i] je %d\n", s->pgs[i]);

        if (mappages(curproc->pgdir, rva, PGSIZE, V2P(s->pgs[i]), PTE_P|PTE_W|PTE_U) != 0) {
            release(&shmtable.lock);
            return -1;
        }
    }

    *va = (void*) cva;
    cprintf("Sta je mapirano %s\n", (char*) (*va));
    release(&shmtable.lock);
    return 0;
}

int
shm_close(int od) {

}