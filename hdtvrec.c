#define GNULINUX
#ifdef GNULINUX
// these two definitely needed
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#endif

// linuxthreads docs say to use this for thread-safe glibc functions
// #define _REENTRANT // defined below

#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <utime.h>
#include <sys/time.h>
#include <sched.h>

/* sqrt needs */
#include <math.h>

/* will need for signal actions, sigterm mostly */
#ifdef SIGNALS
#include <signal.h>
#endif

/* statfs to get free space */
#include <sys/vfs.h>

/* openlog syslog closelog */
#include <syslog.h>


#include <linux/videodev.h>

/*
Author: Kevin Fowlks
Purpose: Record ATSC stream to disk.

Place holder for DTV recoder that handles mult channels in a TS stream.

Plan of attack 

Issue PID's Filter

Thread 1: Writer
	  Read data from ATSC device and write to FIFO
	  Monitor signal power fire signal
Thread 2: Reader
	  Consume data from FIFO	  
	  Sub-Thread 3
	  Create state-machine for PISP information | Lock updates to this data so to minimze contention.	  
*/

int main( int argc , char *argv[]  )
{
	/* Nothing Yet */
	
	return 0;
}
