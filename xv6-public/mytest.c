#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "syscall.h"

void with_map_populate_and_map_anonymous()
{
	int size = PGSIZE;
	// int fd=open("README",O_RDWR);
	int fd = -1;
	printf(1, "before mmap %d \n", freemem());

	int* text =(int*) mmap(0, size, PROT_READ, MAP_POPULATE | MAP_ANONYMOUS, fd, 0);
	printf(1, "after mmap %d \n", freemem());
	for(int i=0;i<size;i+=sizeof(int*)){
		printf(1, "%d", *(text+i));
	}

	munmap((uint)text);
	printf(1, "after munmap %d \n", freemem());
	return;
}

void with_map_populate_and_without_map_anonymous()
{
	int size = PGSIZE;
	int fd = open("README", O_RDWR);
	printf(1, "before mmap %d \n", freemem());

	char* text =(char*) mmap(0, size, PROT_READ|PROT_WRITE, MAP_POPULATE , fd, 64);
	printf(1, "after mmap %d \n", freemem());
	for(int i=0;i<size;++i){
		printf(1, "%c", *(text+i));
	}

	munmap((uint)text);
	printf(1, "after munmap %d \n", freemem());
	return;
}

void without_map_populate_and_without_map_anonymous()
{
	int size = PGSIZE;
	int fd = open("README", O_RDWR);
	printf(1, "before mmap %d \n", freemem());

	char* text =(char*) mmap(0, size, PROT_READ|PROT_WRITE, 0 , fd, 0);
	printf(1, "after mmap %d \n", freemem());
	for(int i=0;i<size;++i){
		printf(1, "%c", *(text+i));
	}

	munmap((uint)text);
	printf(1, "after munmap %d \n", freemem());
	return;
}

void without_map_populate_and_map_anonymous()
{
	int size = PGSIZE;
	int fd = -1;
	printf(1, "before mmap %d \n", freemem());

	int* text =(int*) mmap(0, size, PROT_READ, MAP_ANONYMOUS, fd, 0);
	printf(1, "after mmap %d \n", freemem());
	// for(int i=0;i<size;i+=sizeof(int*)){
	// 	printf(1, "%d", *(text+i));
	// }

	munmap((uint)text);
	printf(1, "after munmap %d \n", freemem());
	return;
}



int main(int argc, char **argv)
{
	//with_map_populate_and_map_anonymous();
	//with_map_populate_and_without_map_anonymous();

	without_map_populate_and_map_anonymous();
	exit();
}
