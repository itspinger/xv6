#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
	return fork();
}

int
sys_exit(void)
{
	exit();
	return 0;  // not reached
}

int
sys_wait(void)
{
	return wait();
}

int
sys_kill(void)
{
	int pid;

	if(argint(0, &pid) < 0)
		return -1;
	return kill(pid);
}

int
sys_shm_open(void) {
	char* name;
	if (argstr(0, &name) < 0) {
		return -1;
	}

	return shm_open(name);
}

int
sys_shm_trunc(void) {
	int od, sz;
	if (argint(0, &od) < 0 || argint(1, &sz) < 0) {
		return -1;
	}

	return shm_trunc(od, sz);
}

int
sys_shm_map(void) {
	int od, flags;
	void** va;
	if (argint(0, &od) < 0 || argptr(1, (void**)&va, sizeof(void*)) < 0 || argint(2, &flags) < 0) {
		return -1;
	}

	// Dodati handle za VA
	return shm_map(od, va, flags);
}

int
sys_shm_close(void) {
	int od;
	if (argint(0, &od) < 0) {
		return -1;
	}

	return shm_close(od);
}

int
sys_getpid(void)
{
	return myproc()->pid;
}

int
sys_sbrk(void)
{
	int addr;
	int n;

	if(argint(0, &n) < 0)
		return -1;
	addr = myproc()->sz;
	if(growproc(n) < 0)
		return -1;
	return addr;
}

int
sys_sleep(void)
{
	int n;
	uint ticks0;

	if(argint(0, &n) < 0)
		return -1;
	acquire(&tickslock);
	ticks0 = ticks;
	while(ticks - ticks0 < n){
		if(myproc()->killed){
			release(&tickslock);
			return -1;
		}
		sleep(&ticks, &tickslock);
	}
	release(&tickslock);
	return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
	uint xticks;

	acquire(&tickslock);
	xticks = ticks;
	release(&tickslock);
	return xticks;
}
