// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
	struct spinlock lock;
	// Ovaj niz nam sluzi samo da bismo alocirali
	// I inicijalizovali i ulancavali podatke
	// Sluzi nam iskljucivo da bismo podatke u njemu ostavljali
	// U binitu
	struct buf buf[NBUF];

	// Linked list of all buffers, through prev/next.
	// head.next is most recently used.
	struct buf head;
} bcache;

void
binit(void)
{
	struct buf *b;

	initlock(&bcache.lock, "bcache");

	// Create linked list of buffers
	// Stavljamo head da prvo pokazuje na sebe
	// Sa prev i next
	bcache.head.prev = &bcache.head;
	bcache.head.next = &bcache.head;
	// Prolazimo kroz niz buf
	for(b = bcache.buf; b < bcache.buf+NBUF; b++){
		// Uzimamo novi node stavljamo mu next na head next
		// Previous mu stavljamo na head cacha
		b->next = bcache.head.next;
		b->prev = &bcache.head; // Tj prva stvar posle heada
		initsleeplock(&b->lock, "buffer");
		bcache.head.next->prev = b; // Ono sto je pre bilo posle heada stavljamo na b
		bcache.head.next = b; // Headov next stavljamo na b
	}
}
// Nakon binit funckije imamo duplo ulancanu listu
// Koja ukljucuje svih 30 elemenata, uvezana je sa headom
// I svaki buf (element) ima svoj lock koji je funkcionalan

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
	struct buf *b;

	acquire(&bcache.lock);

	// Is the block already cached?
	for(b = bcache.head.next; b != &bcache.head; b = b->next){
		// Probamo da nadjemo podatak?
		// Ako ga ima, povecaj broj korisnika i vrati ga
		if(b->dev == dev && b->blockno == blockno){
			b->refcnt++;
			release(&bcache.lock);
			acquiresleep(&b->lock);
			return b;
		}
	}

	// Not cached; recycle an unused buffer.
	// Even if refcnt==0, B_DIRTY indicates a buffer is in use
	// because log.c has modified it but not yet committed it.
	for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
		// Nasli smo slobodno mesto u kesu
		// Oslobadjamo podatak i koristimo novi
		if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
			b->dev = dev;
			b->blockno = blockno;
			b->flags = 0;
			b->refcnt = 1;
			release(&bcache.lock);
			acquiresleep(&b->lock);
			return b;
		}
	}
	panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
// Cita blok (bafer), ako postoji u kesu odmah ga vraca
// Ako ne postoji u kesu, cita se sa diska
struct buf*
bread(uint dev, uint blockno)
{
	struct buf *b;

	// Kazemo mu sa kog uredjaja hocemo koji blok
	// I uradimo bget sa te 2 vrednosti
	b = bget(dev, blockno);
	// Proverava da li je blok validan, ako nije
	// Treba da se procita
	if((b->flags & B_VALID) == 0) {
		// Zove iderw koji 
		// Ako je uslo u drugu petlju iz bget
		// Cita se sa diska da se popuni bafer
		iderw(b);
	}
	// Vrati dobijeni blok
	return b;
}

// Write b's contents to disk.  Must be locked.
// Oznacava fajl kao spreman za pisanje (B_DIRTY)
void
bwrite(struct buf *b)
{
	if(!holdingsleep(&b->lock))
		panic("bwrite");
	b->flags |= B_DIRTY;
	// Radi pisanje agresivno
	iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
	if(!holdingsleep(&b->lock))
		panic("brelse");

	releasesleep(&b->lock); // Oslobadjamo lock od bafera

	acquire(&bcache.lock);
	b->refcnt--;
	// Ako je ref cnt 0, to znaci da bafer nije koriscen
	// I posto nije koriscen (konacno je skroz slobodan) postaje MRU
	// Guramo ga na pocetak MRU liste
	if (b->refcnt == 0) {
		// no one is waiting for it.
		// Stavljamo bafer na pocetka MRU liste
		// I smanjujemo refcnt
		b->next->prev = b->prev;
		b->prev->next = b->next;
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}

	release(&bcache.lock);
}

