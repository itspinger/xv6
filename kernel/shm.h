#define NPAGES 32
#define PGS(sz) ((sz + PGSIZE - 1) / PGSIZE)
#define ADDR(proc) ((void*) P2V(proc->lva))

struct shmo {
    char name[64]; 
    int ref; // Broj referenca na ovaj objekat (kao file.h)  
    int size; // Velicina shmo objekta (maksimum 32 stranice, tj 32 * 4096)
    char *pgs[NPAGES]; // Stranice alociranih za ovaj objekat
};



