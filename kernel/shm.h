#define NPAGES 32

struct shmo {
    char name[64]; 
    int ref; // Broj referenca na ovaj objekat (kao file.h)  
    int size; // Velicina shmo objekta (maksimum 32 stranice, tj 32 * 4096)
    char *pgs[NPAGES]; // Broj stranica alociranih za ovaj objekat
};



