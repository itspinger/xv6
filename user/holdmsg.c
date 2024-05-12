#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    int shm = shm_open("shm_demo");
    if (shm < 0) {
        printf("failed to open shm\n");
        exit();
    }

    shm_trunc(shm, 4096);
    printf("holding message as pid %d..\n kill to forget\n", getpid());
    
    while (1)
        sleep(9999);

	exit();
}
