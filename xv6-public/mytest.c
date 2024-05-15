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

void with_map_populate(){
	int size=8192;
	int fd=open("README",O_RDWR);
	char* text=mmap(0,size,PROT_READ,MAP_POPULATE|MAP_ANONUMOUS,fd,0);

	for(int i=0;i<size;i++)
	{
		printf(1,"%c",text[i]);
	}

	munmap(text);
	return;
}

void without_map_populate(){
	int size=8192;
	int fd=open("README",O_RDWR);
	char* text=mmap(0,size,PROT_READ,0,fd,0);

	for(int i=0;i<size;i++)
	{
		printf(1,"%c",text[i]);
	}

	munmap(text);
	return;
}

int main(int argc, char **argv)
{
	

}
