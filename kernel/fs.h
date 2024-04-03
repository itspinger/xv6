// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
//
//
// Ovo je super blok u file sistemu (sadrzi opis narednih oblasti)
// Vidimo da nemamo broj bitmape (ona je izracunljiva iz size promenljive)
struct superblock {
	uint size;         // Velicina file sistema u broju blokova
	uint nblocks;      // Broj data blokova
	uint ninodes;      // Broj inode blokova
	uint nlog;         // Broj log blokova
	uint logstart;     // Pocetak loga - Granica izmedju super bloka i loga
	uint inodestart;   // Pocetak inode - Granica izmedju log i inode
	uint bmapstart;    // Pocetak bitmap - Granica izmedju inode i bitmape
	// Ne cuvamo pocetak data sekcije jer se ona nalazi odmah posle bitmape
	// Posto znamo gde pocinje bitmap i gde se zavrsava pa je lako izracunljivo
	// Ovu superblok strukturu generise mkfs.c
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
// Znaci XV6 ima inodove
struct dinode {
	short type;           // File type
	// Major i minor opisuju o kom se uredjaju radi
	// Na UNIX sistemima sve su "fajlovi", tj sve su fajl deskriptori
	// "ls /dev"
	// Zasto su bitni ovi major i minor brojevi?
	// "ls /dev/ttyS0" -> (T_DEV)
	short major;          // Major device number (T_DEV only)
	short minor;          // Minor device number (T_DEV only)
	short nlink;          // Broj imena za neki fajl (takodje u XV6, broj puta koji je otvoren neki fajl)
	uint size;            // Stvarni broj blokova u fajlu - Razlicitost stvarne duzine fajla od fizicke duzine fajla
	uint addrs[NDIRECT+1];   // Realan broj blokova dolazi u igru sa addrs (omogacava eksternu fragmentaciju)
	// Na osnovu addrs niza, znamo gde su podaci na disku
	char symlink[192];
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
	ushort inum;
	char name[DIRSIZ];
};

