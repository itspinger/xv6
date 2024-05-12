#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    printf("hello\n");
	if (argc != 2) {
        printf("usage: %s [message]\n", argc > 0 ? argv[0] : "setmsg");
        exit();
    }

    int shm = shm_open("shm_demo");
    if (shm < 0) {
        printf("failed to open shm\n");
        exit();
    }

    shm_trunc(shm, 4096);
    void* shm_reg_;
    if (shm_map(shm, &shm_reg_, O_RDWR) < 0) {
        printf("failed to map shm\n");
        exit();
    }

    char* shm_reg = shm_reg_;
    strncpy(shm_reg_, argv[1], strlen(argv[1]));
    shm_reg[4095] = 0;
	exit();
}
