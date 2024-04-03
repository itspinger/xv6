#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	if (argc != 3) {
        printf("sln: usage sln <dest> <link>\n");
        exit();
    }

    if (symlink(argv[1], argv[2]) < 0) {
        printf("sln: failed to link %s to %s\n", argv[2], argv[1]);
        exit();
    }

    printf("successfully linked %s to %s\n", argv[2], argv[1]);
	exit();
}
