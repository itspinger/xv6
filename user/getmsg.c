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
    void* shm_reg_;
    if (shm_map(shm, &shm_reg_, O_RDWR) < 0) {
        printf("failed to map shm\n");
        exit();
    }

    const char* shm_reg = shm_reg_;
    printf("Region is %d\n", shm_reg);
    printf("message: %s\n", shm_reg);
	exit();
}
