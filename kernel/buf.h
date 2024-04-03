// Ovo je jedan podatak iz kesa
struct buf {
	int flags;
	// Dev i blockno unikatno identifikuju svaki blok
	// Sluze nam da znamo o kom podatku pricamo
	uint dev; // Disk na kome se nalazi dati blok
	uint blockno; // Broj bloka
	struct sleeplock lock;
	uint refcnt;
	struct buf *prev; // LRU cache list
	struct buf *next;
	struct buf *qnext; // disk queue
	uchar data[BSIZE]; // Sadrzaj podatka na disku
};
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk

