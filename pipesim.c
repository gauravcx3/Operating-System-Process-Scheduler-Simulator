#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*  CITS2002 Project 1 2020
    Name:                GAURAV CHAKRAVERTY
    Student number(s):   22750993
 */


//  MAXIMUM NUMBER OF PROCESSES OUR SYSTEM SUPPORTS (PID=1..20)
#define MAX_PROCESSES                       20

//  MAXIMUM NUMBER OF SYSTEM-CALLS EVER MADE BY ANY PROCESS
#define MAX_SYSCALLS_PER_PROCESS            50

//  MAXIMUM NUMBER OF PIPES THAT ANY SINGLE PROCESS CAN HAVE OPEN (0..9)
#define MAX_PIPE_DESCRIPTORS_PER_PROCESS    10

//  TIME TAKEN TO SWITCH ANY PROCESS FROM ONE STATE TO ANOTHER
#define USECS_TO_CHANGE_PROCESS_STATE       5

//  TIME TAKEN TO TRANSFER ONE BYTE TO/FROM A PIPE
#define USECS_PER_BYTE_TRANSFERED           1

// JUST A START VALUE (CAN BE USED ANYWHERE NEEDED)
#define UNKNOWN 							-1
// To indicate that a system call has fininshed executing
#define FINISHED							-1


//  ---------------------------------------------------------------------

//  YOUR DATA STRUCTURES, VARIABLES, AND FUNCTIONS SHOULD BE ADDED HERE:

int timetaken       = 0;

//this will be our ready queue
int readyq[MAX_PROCESSES];
//keeps count of how many processes are in readyq
int qitems 			= 0;
//keeps count of how many processes are sleeping
int sleepingcount 	= 0;
//keeps count of how many processes are waiting
int waitingcount 	= 0;
//pid of process taken out from the head of readyq
int topout;
//indicates if it is okay to halt
int safetohalt 		= 0;
//indicates if anything is running
int running			= 0;
//keeps count of how many processes have yet to exit
int premain			= 0;
//indicates if cpu is sleeping
int cpusleep		= 0;
//keeps count of readblocks
int readblocks		= 0;
//keeps count of writeblocks
int writeblocks		= 0;
//temporarily store an id that can be accesed anywhere
int globaltempid	= 0;

int globaltimequantum 	= 0;
int globalpipesize		= 0; 

//different states process can be in
enum {NEW, READY, RUNNING, EXITED, SLEEPING, WAITING, WRITEBLOCKED, READBLOCKED ,NONEXISTENT};
//different possible system calls
enum {SYS_COMPUTE, SYS_EXIT, SYS_FORK, SYS_SLEEP, SYS_WAIT, SYS_PIPE, SYS_WRITE, SYS_READ};
enum {YES, NO};

struct
{
	int state;									//state of the process
	int nextsyscall;							//next system call to the executed for the process
	int readenabled;							//can this process read? (if a child)
	int writeenabled;							//can this process write? (if a parent)
	int transferremain;							//amount of remainig data tranfer
	int waketarget;								//when to wake up from sleep
	struct 
	{
		int syscall;							//system call that needs to be executed
		int usecs;								//some system calls have time associated with it
		int childpid;							//pid of the child that need to be forked
		int waitpid;							//pid of the process we may be waiting for
		int pipeid;								//id of any pipe to be created/written/read
		int nbytes;								//bytes to read/write
	} syscalls[MAX_SYSCALLS_PER_PROCESS];		//store list of system calls for the process in array
	struct 
	{
		int entryvalid;							//do not bother reading if entry not set to valid
		int pipewritingto;						//id of pipe(s) writing to
		int pipereadingfrom;					//id of pipe reading from parent
		int pipeconnected;						//is pipe connected on both ends?
		int datainpipe;							//amount of data currently inside pipe
	} pipes[MAX_PIPE_DESCRIPTORS_PER_PROCESS];	//store detais and state of pipes
} process[MAX_PROCESSES];						//store the list of processes

//  ---------------------------------------------------------------------

void init_processes(void)
{	//just initializing the things inside the process structure
	for(int p=0; p<MAX_PROCESSES; p = p+1)
	{
		//state of the individual process (at first they do not exist)
		process[p].state 			= NONEXISTENT;
		//default value for the next system call to be executed (start at 0)
		process[p].nextsyscall 		= 0;
		process[p].readenabled 		= NO;
		process[p].writeenabled 	= NO;
		process[p].transferremain 	= 0;
		process[p].waketarget 		= NO;
		//initializing the things inside the syscalls struture/array as well
		for (int s=0; s<MAX_SYSCALLS_PER_PROCESS; s=s+1)
		{
			process[p].syscalls[s].syscall 	= 0;
			process[p].syscalls[s].usecs 	= 0;
			process[p].syscalls[s].childpid = 0;
			process[p].syscalls[s].waitpid 	= 0;
			process[p].syscalls[s].pipeid 	= 0;
			process[p].syscalls[s].nbytes 	= 0;
			
		}
		//initializing the things inside the pipes structure/array as well
		for (int d=0; d<MAX_PIPE_DESCRIPTORS_PER_PROCESS; d=d+1)
		{
			process[p].pipes[d].entryvalid 		= NO;
			process[p].pipes[d].pipewritingto 	= UNKNOWN;
			process[p].pipes[d].pipereadingfrom = UNKNOWN;
			process[p].pipes[d].pipeconnected	= NO;
			process[p].pipes[d].datainpipe		= 0;
		}
		
	}
	
	//populating readyq with 0
	for (int i = 0; i < MAX_PROCESSES; i = i + 1)
	{
		readyq[i] = 0;
	}	
}

//add things to bottom of readyq
void addready(int pid)
{
	readyq[qitems] = pid;
	qitems = qitems + 1;
}

//take out pid at the head of readyq
int takeready(void)
{
	topout = readyq[0];
	qitems = qitems - 1;
	for (int i = 0; i<MAX_PROCESSES; i = i + 1)
	{
		readyq[i] = readyq[i + 1];
	}
	readyq[qitems+1] = 0;
	return topout;
}

//just to debug
void debug(void)
{
	printf("running->		%i\n", running);
	printf("premain->		%i\n", premain);
	printf("waitingcount->		%i\n", waitingcount);
	printf("sleepingcount->		%i\n", sleepingcount);
	printf("qitems->		%i\n", qitems);
	printf("writeblocks->		%i\n", writeblocks);
	printf("readblocks->		%i\n", readblocks);
}

//changes the state of a process
void setstate(int pid, int state)
{
	process[pid].state = state;
}

//gets the state of a process
int getstate(int pid)
{
	int state = process[pid].state;
	return state;
}

//gets the systemcall to be executed for process
int getsyscall(int pid)
{
	int currentsyscall;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if (process[pid].syscalls[s].syscall != FINISHED)
		{
			currentsyscall = process[pid].syscalls[s].syscall;
			break;
		}
	}
	return (currentsyscall);
}

//updates the systemcall of a process
void setsyscall(int pid, int syscall)
{
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if (process[pid].syscalls[s].syscall != FINISHED)
		{
			process[pid].syscalls[s].syscall = syscall;
			break;
		}
	}
}

//exits the process
void exitprocess(int pid)
{
	setstate(pid, EXITED);
	setsyscall(pid, FINISHED);
	printf("@%i		p%i:exit(), p%i.RUNNING->EXITED \n", timetaken, pid+1, pid+1);
	premain--;
}

//gets compute time from the pid system call
int getcompute(int pid)
{
	int time;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_COMPUTE))
		{
			time = process[pid].syscalls[s].usecs;
			break;
		}
	}
	return (time);
}

//sets or updates the remaining process compute time
void setcompute(int pid, int time)
{
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_COMPUTE))
		{
			process[pid].syscalls[s].usecs = time;
			break;
		}
	}
}

//runs the compute process for pid
void compute(int pid)
{
	setstate(pid, RUNNING);
	int computeremain = getcompute(pid);
	if (computeremain > globaltimequantum)
	{
		computeremain = computeremain - globaltimequantum;
		printf("@%i		p%i:compute(), for %iusec (%i usecs remaining) \n", 
				timetaken, pid+1, globaltimequantum, computeremain);
		timetaken  	  = timetaken + globaltimequantum;
		setcompute(pid, computeremain);
	}
	else if (computeremain <= globaltimequantum)
	{	
		printf("@%i		p%i:compute(), for %iusec (now completed) \n", 
				timetaken, pid+1, computeremain);
		timetaken	  = timetaken + computeremain;
		computeremain = 0;
		setcompute(pid, computeremain);
		setsyscall(pid, FINISHED);
	}
}

//just get the details of the child that needs to be forked
int getfork(int pid)
{
	int child;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_FORK))
		{
			child = process[pid].syscalls[s].childpid;
			break;
		}
	}
	return (child);
}

//checks if the parent has any free pipes to connect to
void checkpipes(int parent, int child)
{
	for (int s = 0; s<(MAX_PIPE_DESCRIPTORS_PER_PROCESS); s=s+1)
	{
		if ((process[parent].pipes[s].entryvalid == YES)&&
			((process[parent].pipes[s].pipeconnected == NO)))
		{
			process[parent].pipes[s].pipeconnected = YES;
			process[parent].writeenabled = YES;
			process[child].readenabled = YES;
			break;
		}
	}
}

//forks to create new child from parent
void newfork(int parent)
{
	int child;
	//get the pid of the child 
	child = getfork(parent);
	setstate(child, READY);
	addready(child);
	premain++;
	printf("@%i		p%i.fork(), new childPID=%i, p%i.NEW->READY			Items in RQ: %i\n", 
			timetaken, parent+1, child+1, child+1, qitems);
	timetaken = timetaken + 5;
	setsyscall(parent, FINISHED);
	setstate(parent, READY);
	addready(parent);
	printf("@%i		p%i.RUNNING->READY 						Items in RQ: %i\n", 
			timetaken, parent+1, qitems);
	timetaken = timetaken + 5;
	running = 0;
	//connect child to any available parent pipe
	checkpipes(parent, child);
}

//get the pid of the process we need to wait for
int getwait(int pid)
{
	int waitpid;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_WAIT))
		{
			waitpid = process[pid].syscalls[s].waitpid;
			break;
		}
	}
	return waitpid;
}

//wait for a given process
void waitprocess(int pid)
{
	int waitpid = getwait(pid);
	if (getstate(pid) != WAITING)
	{
		setstate(pid, WAITING);
		waitingcount++;
		printf("@%i		p%i.wait(), for childPID=%i, p%i.RUNNING->WAITING			Items in RQ: %i\n", 
			timetaken, pid+1, waitpid+1, pid+1, qitems);
		timetaken = timetaken + 5;
		debug();
		running = 0;
	}
	//if process we are waiting for has exited
	else if (getstate(waitpid) == EXITED)
	{
		timetaken = timetaken - 1;
		//add back to ready queue
		setstate(pid, READY);
		setsyscall(pid, FINISHED);
		addready(pid);
		waitingcount--;
		printf("@%i		p%i.exit(), p%i has been waiting for p%i, p%i.WAITING->READY	Items in RQ: %i\n", 
			timetaken, waitpid+1, pid+1, waitpid+1, pid+1, qitems);
		timetaken = timetaken + 5;
		running = 0;
	}
}

//update waketarget for the given process
void setwaketarget(int pid, int usecs)
{
	process[pid].waketarget = usecs;
}

//get sleep time for the given process
int getsleep(int pid)
{
	int time;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_SLEEP))
		{
			time = process[pid].syscalls[s].usecs;
			break;
		}
	}
	return (time);
}

//given process sleeps until target time has passed
void sleepprocess(int pid)
{
	int sleeptime = getsleep(pid);
	if ((getstate(pid)) != SLEEPING)
	{
		setstate(pid, SLEEPING);
		//target time to wakeup
		setwaketarget(pid, (timetaken + sleeptime));
		sleepingcount++;
		printf("@%i		p%i.sleep() for %iusecs, p%i.RUNNING->SLEEPING			Items in RQ: %i\n", 
			timetaken, pid+1, sleeptime, pid+1, qitems);
		timetaken = timetaken + 5;
		running = 0;
	}
	//if waketarget time has been met or crossed
	else if (timetaken >= (process[pid].waketarget))
	{
		timetaken = timetaken + 5;
		if (cpusleep==1)
		{
			cpusleep = 0;
			printf("@%i		CPU is idle, time has advanced.\n", timetaken);
		}
		//add back to ready queue
		setstate(pid, READY);
		setsyscall(pid, FINISHED);
		addready(pid);
		sleepingcount--;
		printf("@%i		p%i finishes sleeping, p%i.SLEEPING->READY			Items in RQ: %i\n", 
			timetaken, pid+1, pid+1, qitems);
		timetaken = timetaken + 5;
	}
}

//get id of pipe to be created
int getpipe(int pid)
{
	int pipeid;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_PIPE))
		{
			pipeid = process[pid].syscalls[s].pipeid;
			break;
		}
	}
	return pipeid;
}

//update the details of the pipe
void setpipe(int pid, int pipeid)
{
	for (int s = 0; s<(MAX_PIPE_DESCRIPTORS_PER_PROCESS); s=s+1)
	{
		if ((process[pid].pipes[s].entryvalid != YES))
		{
			process[pid].pipes[s].entryvalid = YES;
			process[pid].pipes[s].pipewritingto = pipeid;
			break;
		}
	}
}

//create new pipe
void makepipe(int pid)
{
	
	int pipeid = getpipe(pid);
	setpipe(pid, pipeid);
	//add back to ready queue
	setstate(pid, READY);
	setsyscall(pid, FINISHED);
	addready(pid);
	printf("@%i		p%i.pipe(), pipedesc=%i, p%i.RUNNING->READY			Items in RQ: %i\n", 
			timetaken, pid+1, pipeid+1, pid+1, qitems);
	timetaken = timetaken + 5;
	running = 0;
}

//just get the pipeid and bytes to write
int getwrite(int pid)
{
	int pipeid;
	int data;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_WRITE))
		{
			pipeid = process[pid].syscalls[s].pipeid;
			data   = process[pid].syscalls[s].nbytes;
			process[pid].transferremain = data;
			break;
		}
	}
	return (pipeid);
}

//just get the pipeid and bytes to write
int getread(int pid)
{
	int pipeid;
	int data;
	for (int s = 0; s<(process[pid].nextsyscall); s=s+1)
	{
		if ((process[pid].syscalls[s].syscall != FINISHED) && 
			(process[pid].syscalls[s].syscall == SYS_READ))
		{
			pipeid = process[pid].syscalls[s].pipeid;
			data   = process[pid].syscalls[s].nbytes;
			process[pid].transferremain = data;
			break;
		}
	}
	return (pipeid);
}

//gets which record corrosponds to the pipeid
int getpiperecord(int pid, int pipeid)
{
	int record;
	for(int p=0; p<MAX_PROCESSES; p = p+1)
	{
		for (int s = 0; s<(MAX_PIPE_DESCRIPTORS_PER_PROCESS); s=s+1)
		{
			if ((process[p].pipes[s].pipewritingto == pipeid))
			{
				record = s;
				globaltempid = p;
				break;
			}
		}
	}
	return (record);
}

//process writes to a pipe
void writepipe(int pid)
{
	int pipeid    		 = getwrite(pid);
	int transferremain   = process[pid].transferremain;
	int record 	   		 = getpiperecord(pid, pipeid);
	int datainpipe 		 = process[pid].pipes[record].datainpipe;
	int pipecapacityleft = globalpipesize - datainpipe;
	//check if pipe is already full
	if (pipecapacityleft == 0)
	{
		if (getstate(pid) != WRITEBLOCKED)
		{
			writeblocks++;
		}
		setstate(pid, WRITEBLOCKED);
		running = 0;
	}
	//if pipe is not full but less empty then remaining transfer
	else if ((pipecapacityleft > 0) && (pipecapacityleft < transferremain))
	{	
		if (getstate(pid) == WRITEBLOCKED)
		{
			timetaken = timetaken + 5 - 1;
			addready(pid);
			setstate(pid, READY);
			printf("@%i		p%i can write its pipedesc=%i, p%i.WRITELOCKED->READY	\n", 
				timetaken, pid+1, pipeid+1, pid+1);
			timetaken = timetaken + 5;
			writeblocks--;
		}
		else if (getstate(pid) != WRITEBLOCKED)
		{
			//update remaining bytes to transfer
			transferremain = transferremain - pipecapacityleft;
			process[pid].transferremain = transferremain;
			//pipe is now full
			process[pid].pipes[record].datainpipe = globalpipesize;
			if (getstate(pid) != WRITEBLOCKED)
			{
				writeblocks++;
			}
			setstate(pid, WRITEBLOCKED);
			running = 0;
			printf("@%i		p%i.writepipe() %i bytes to pipedesc=%i	\n", 
					timetaken, pid+1, pipecapacityleft, pipeid+1);
			printf("@%i		p%i.RUNNING->WRITEBLOCKED		Items in RQ: %i\n", 
					timetaken, pid+1, qitems);
			timetaken = timetaken + pipecapacityleft;
		}
	}
	//if pipe is not full and more empty than remainng transfer
	else if ((pipecapacityleft > 0) && (pipecapacityleft >= transferremain))
	{	
		if (getstate(pid) == WRITEBLOCKED)
		{
			timetaken = timetaken + 5 - 1;
			addready(pid);
			setstate(pid, READY);
			printf("@%i		p%i can write its pipedesc=%i, p%i.WRITELOCKED->READY	\n", 
				timetaken, pid+1, pipeid+1, pid+1);
			timetaken = timetaken + 5;
			writeblocks--;
		}
		else if (getstate(pid) != WRITEBLOCKED)
		{
		//update remainng pipe capacity
			pipecapacityleft = pipecapacityleft - transferremain;
			process[pid].pipes[record].datainpipe = datainpipe + transferremain;
			//transfer is complete
			process[pid].transferremain = 0;
			setstate(pid, READY);
			setsyscall(pid, FINISHED);
			running = 0;
			printf("@%i		p%i.writepipe() %i bytes to pipedesc=%i	(completed), p%i.RUNNING->READY\n", 
					timetaken, pid+1, transferremain, pipeid+1, pid+1);
			timetaken = timetaken + transferremain;
			addready(pid);
			timetaken = timetaken + 5;
			if (writeblocks>0)
			{
				writeblocks--;
			}
		}
	}
}

//process reads from a pipe
void readpipe(int pid)
{
	int pipeid    		 = getread(pid);
	int record 	   		 = getpiperecord(pid, pipeid);
	int transferremain   = process[pid].transferremain;
	int datainpipe 		 = process[globaltempid].pipes[record].datainpipe;
	//check if pipe is empty
	if ((datainpipe == 0) && (getstate(pid) != READBLOCKED))
	{
		if (getstate(pid) != READBLOCKED)
		{
			readblocks++;
		}
		setstate(pid, READBLOCKED);
		running = 0;
		printf("@%i		p%i.readpipe() from empty pipedesc=%i, p%i.RUNNING->READBLOCKED	\n", 
				timetaken, pid+1, pipeid+1, pid+1);
		timetaken = timetaken + 5;
	}
	//if pipe is not empty and data in pipe is less than remaining transfer
	else if ((datainpipe > 0) && (datainpipe < transferremain))
	{	
		if (getstate(pid) == READBLOCKED)
		{
			timetaken = timetaken + 5 - 1;
			addready(pid);
			setstate(pid, READY);
			printf("@%i		p%i can read its pipedesc=%i, p%i.READLOCKED->READY	\n", 
				timetaken, pid+1, pipeid+1, pid+1);
			timetaken = timetaken + 5;
			readblocks--;
		}
		else if (getstate(pid) != READBLOCKED)
		{
			//update remaining bytes to transfer
			transferremain = transferremain - datainpipe;
			process[pid].transferremain = transferremain;
			//pipe is empty
			process[globaltempid].pipes[record].datainpipe = 0;
			if (getstate(pid) != READBLOCKED)
			{
				readblocks++;
			}
			printf("@%i		p%i.readpipe() %i bytes from pipedesc=%i, p%i.RUNNING->READBLOCKED\n", 
				timetaken, pid+1, datainpipe, pipeid+1, pid+1);
			setstate(pid, READBLOCKED);
			timetaken = timetaken + datainpipe + 1;
		}
	}
	//if pipe is not empty and data in pipe greater or equal to remaining transfer
	else if ((datainpipe > 0) && (datainpipe >= transferremain))
	{
		if (getstate(pid) == READBLOCKED)
		{
			addready(pid);
			setstate(pid, READY);
			printf("@%i		p%i can read its pipedesc=%i, p%i.READLOCKEDYY->READY	\n", 
				timetaken, pid+1, pipeid+1, pid+1);
			timetaken = timetaken + 5;
			readblocks--;
		}
		else if (getstate(pid) != READBLOCKED)
		{
			printf("@%i		p%i.readpipe() %i bytes from pipedesc=%i	(completed), p%i.RUNNING->READY\n", 
					timetaken, pid+1, transferremain, pipeid+1, pid+1);
			timetaken = timetaken + transferremain;
			running = 0;
			setsyscall(pid, FINISHED);
			process[globaltempid].pipes[record].datainpipe = datainpipe - transferremain;
			//update remaining bytes to transfer
			transferremain = 0;
			process[pid].transferremain = transferremain;
			setstate(pid, READY);
			addready(pid);
			timetaken = timetaken + 5;
		}
	}
}

//runs the process
void run(int pid)
{	
	int currentsyscall;
	//something is running
	running 		= 1;
	setstate(pid, RUNNING);
	currentsyscall	= getsyscall(pid);
	if (currentsyscall == SYS_EXIT)
	{
		exitprocess(pid);
		timetaken 		= timetaken + 5;
		//finished running current process
		running 		= 0;
	}
	else if (currentsyscall == SYS_COMPUTE)
	{
		compute(pid);
		setstate(pid, READY);
		//finished running current process
		running   = 0;
		//add process to ready queue
		addready(pid);
		printf("@%i		p%i.RUNNING->READY 						Items in RQ: %i\n", 
				timetaken, pid+1, qitems);
		timetaken = timetaken + 5;
	}
	else if (currentsyscall == SYS_FORK)
	{
		newfork(pid);
	}
	else if (currentsyscall == SYS_WAIT)
	{
		waitprocess(pid);
	}
	else if (currentsyscall == SYS_SLEEP)
	{
		sleepprocess(pid);
	}
	else if (currentsyscall == SYS_PIPE)
	{
		makepipe(pid);
	}
	else if (currentsyscall == SYS_WRITE)
	{
		writepipe(pid);
	}
	else if (currentsyscall == SYS_READ)
	{
		readpipe(pid);
	}
}

//runs a set of instructions at boot
void boot(void)
{	
	premain++;
	printf("@%i		BOOT, p%i.RUNNING \n", timetaken, 0+1);
	//the first process should be there at boot
	run(0);

}

//checks if okay to halt
void checkhalt(void)
{
	if (premain == 0)
	{
		printf("@%i		HALT (no processes alive) \n", timetaken);
	}
}

//check if the waiting processes can resume
void checkwaitsleepblocks(void)
{	
	if (waitingcount>0)
	{
		for(int p=0; p<MAX_PROCESSES; p = p+1)
		{
			if ((process[p].state) == WAITING)
			{
				waitprocess(p);
			}
		}
	}
	if (sleepingcount>0)
	{
		for(int p=0; p<MAX_PROCESSES; p = p+1)
		{
			if ((process[p].state) == SLEEPING)
			{
				sleepprocess(p);
			}
		}
	}
	if (writeblocks>0)
	{
		for(int p=0; p<MAX_PROCESSES; p = p+1)
		{
			if ((process[p].state) == WRITEBLOCKED)
			{
				writepipe(p);
			}
		}
	}
	if (readblocks>0)
	{
		for(int p=0; p<MAX_PROCESSES; p = p+1)
		{
			if ((process[p].state) == READBLOCKED)
			{
				readpipe(p);
			}
		}
	}
}

//keep advancing time if cpu is idle
void checkcpuidle(void)
{
	if ((qitems==0) && (running==0) && (premain>0))
	{
		timetaken = timetaken + 1;
		cpusleep = 1;
	}
}

void run_simulation(int timequantum, int pipesize)
{
	boot();
	while ((qitems>0) || (waitingcount>0) || (sleepingcount>0) || (writeblocks>0) || (readblocks > 0))
	{	
		//check if cpu is idle and advance time
		checkcpuidle();
		//check and update status of sleeping/waiting/read/write blocked processes
		checkwaitsleepblocks();
		if (qitems>0)
		{
			takeready();
			printf("@%i		p%i.READY->RUNNING \n", timetaken, topout+1);
			timetaken = timetaken + 5;
			run(topout);
		}
	}
	checkhalt();
}

//  ---------------------------------------------------------------------

//  FUNCTIONS TO VALIDATE FIELDS IN EACH eventfile - NO NEED TO MODIFY
int check_PID(char word[], int lc)
{
    int PID = atoi(word);

    if(PID <= 0 || PID > MAX_PROCESSES) {
        printf("invalid PID '%s', line %i\n", word, lc);
        exit(EXIT_FAILURE);
    }
    return PID;
}

int check_microseconds(char word[], int lc)
{
    int usecs = atoi(word);

    if(usecs <= 0) {
        printf("invalid microseconds '%s', line %i\n", word, lc);
        exit(EXIT_FAILURE);
    }
    return usecs;
}

int check_descriptor(char word[], int lc)
{
    int pd = atoi(word);

    if(pd < 0 || pd >= MAX_PIPE_DESCRIPTORS_PER_PROCESS) {
        printf("invalid pipe descriptor '%s', line %i\n", word, lc);
        exit(EXIT_FAILURE);
    }
    return pd;
}

int check_bytes(char word[], int lc)
{
    int nbytes = atoi(word);

    if(nbytes <= 0) {
        printf("invalid number of bytes '%s', line %i\n", word, lc);
        exit(EXIT_FAILURE);
    }
    return nbytes;
}

//  parse_eventfile() READS AND VALIDATES THE FILE'S CONTENTS
//  YOU NEED TO STORE ITS VALUES INTO YOUR OWN DATA-STRUCTURES AND VARIABLES
void parse_eventfile(char program[], char eventfile[])
{
#define LINELEN                 100
#define WORDLEN                 20
#define CHAR_COMMENT            '#'

//  ATTEMPT TO OPEN OUR EVENTFILE, REPORTING AN ERROR IF WE CAN'T
    FILE *fp    = fopen(eventfile, "r");

    if(fp == NULL) {
        printf("%s: unable to open '%s'\n", program, eventfile);
        exit(EXIT_FAILURE);
    }

    char    line[LINELEN], words[4][WORDLEN];
    int     lc = 0;

//  READ EACH LINE FROM THE EVENTFILE, UNTIL WE REACH THE END-OF-FILE
    while(fgets(line, sizeof line, fp) != NULL) {
        ++lc;

//  COMMENT LINES ARE SIMPLY SKIPPED
        if(line[0] == CHAR_COMMENT) {
            continue;
        }

//  ATTEMPT TO BREAK EACH LINE INTO A NUMBER OF WORDS, USING sscanf()
        int nwords = sscanf(line, "%19s %19s %19s %19s",
                                    words[0], words[1], words[2], words[3]);

//  WE WILL SIMPLY IGNORE ANY LINE WITHOUT ANY WORDS
        if(nwords <= 0) {
            continue;
        }

//  ENSURE THAT THIS LINE'S PID IS VALID
        int thisPID = check_PID(words[0], lc);

//  OTHER VALUES ON (SOME) LINES
        int otherPID, nbytes, usecs, pipedesc;
		
//  IDENTIFY LINES RECORDING SYSTEM-CALLS AND THEIR OTHER VALUES
//  THIS FUNCTION ONLY CHECKS INPUT;  YOU WILL NEED TO STORE THE VALUES
        if(nwords == 3 && strcmp(words[1], "compute") == 0) {
            usecs   = check_microseconds(words[2], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is compute
			process[thisPID-1].syscalls[next].syscall 	= SYS_COMPUTE;
			//also store the time to compute
			process[thisPID-1].syscalls[next].usecs		= usecs;
			++process[thisPID-1].nextsyscall;
			
        }
        else if(nwords == 3 && strcmp(words[1], "sleep") == 0) {
            usecs   = check_microseconds(words[2], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is sleep
			process[thisPID-1].syscalls[next].syscall 	= SYS_SLEEP;
			//also store the time to SLEEP
			process[thisPID-1].syscalls[next].usecs		= usecs;
			++process[thisPID-1].nextsyscall;
        }
        else if(nwords == 2 && strcmp(words[1], "exit") == 0) {
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is exit
			process[thisPID-1].syscalls[next].syscall 	= SYS_EXIT;
			++process[thisPID-1].nextsyscall;
        }
        else if(nwords == 3 && strcmp(words[1], "fork") == 0) {
            otherPID = check_PID(words[2], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is fork
			process[thisPID-1].syscalls[next].syscall 	= SYS_FORK;
			//also store the pid of the child to fork
			process[thisPID-1].syscalls[next].childpid	= otherPID-1;
			++process[thisPID-1].nextsyscall;
        }
        else if(nwords == 3 && strcmp(words[1], "wait") == 0) {
            otherPID = check_PID(words[2], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is wait
			process[thisPID-1].syscalls[next].syscall 	= SYS_WAIT;
			//also store the pid of the child to wait for
			process[thisPID-1].syscalls[next].waitpid	= otherPID-1;
			++process[thisPID-1].nextsyscall;
        }
        else if(nwords == 3 && strcmp(words[1], "pipe") == 0) {
            pipedesc = check_descriptor(words[2], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is pipe
			process[thisPID-1].syscalls[next].syscall 	= SYS_PIPE;
			//also store the id of the pipe to be created
			process[thisPID-1].syscalls[next].pipeid	= pipedesc-1;
			++process[thisPID-1].nextsyscall;
        }
        else if(nwords == 4 && strcmp(words[1], "writepipe") == 0) {
            pipedesc = check_descriptor(words[2], lc);
            nbytes   = check_bytes(words[3], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is write
			process[thisPID-1].syscalls[next].syscall 	= SYS_WRITE;
			//also store the id of the pipe to be written to
			process[thisPID-1].syscalls[next].pipeid	= pipedesc-1;
			//also store the number of bytes to be written 
			process[thisPID-1].syscalls[next].nbytes	= nbytes;
			++process[thisPID-1].nextsyscall;
        }
        else if(nwords == 4 && strcmp(words[1], "readpipe") == 0) {
            pipedesc = check_descriptor(words[2], lc);
            nbytes   = check_bytes(words[3], lc);
			//the next system call to be executed by this process
			int next = process[thisPID-1].nextsyscall;
			//for this pid, the next sestem call is read
			process[thisPID-1].syscalls[next].syscall 	= SYS_READ;
			//also store the id of the pipe to be read from
			process[thisPID-1].syscalls[next].pipeid	= pipedesc-1;
			//also store the number of bytes to be read
			process[thisPID-1].syscalls[next].nbytes	= nbytes;
			++process[thisPID-1].nextsyscall;
        }
//  UNRECOGNISED LINE
        else {
            printf("%s: line %i of '%s' is unrecognized\n", program,lc,eventfile);
            exit(EXIT_FAILURE);
        }
    }
    fclose(fp);

#undef  LINELEN
#undef  WORDLEN
#undef  CHAR_COMMENT
}

//  ---------------------------------------------------------------------

//  CHECK THE COMMAND-LINE ARGUMENTS, CALL parse_eventfile(), RUN SIMULATION
int main(int argc, char *argv[])
{
	int timequantum 	= atoi(argv[2]);
	int pipesize 		= atoi(argv[3]);
	globaltimequantum 	= timequantum;
	globalpipesize 		= pipesize;
	//check if argc == 4
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s eventfile timequantum-usecs pipesize-bytes\n", argv[0]);
        exit(EXIT_FAILURE);         // Exit indicating failure
	}
	//check if timequantum > 0
	else if(timequantum <= 0)
	{
		fprintf(stderr, "timequantum provided ->%s<- must be greater than zero\n", argv[2]);
        exit(EXIT_FAILURE);         // Exit indicating failure
	}
	//check if pipesize > 0
	else if(pipesize <= 0)
	{
		fprintf(stderr, "pipesize provided ->%s<- must be greater than zero\n", argv[3]);
        exit(EXIT_FAILURE);         // Exit indicating failure
	}
	else
	{
	//makes sure that everything in the data structures are properly iniitialized
	init_processes();
	//takes in the name of the program and file
	parse_eventfile(argv[0], argv[1]);
	run_simulation(timequantum, pipesize);
	//debug();						//just for testing
	}
    printf("timetaken %i\n", timetaken);
    return 0;
}
