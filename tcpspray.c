/*
*      tcpspray.c  - v1.1
*
*      measure throughput of half (via discard) and full (via echo) duplex
*      tcp connection
*
*      Greg Christy <gmc@quotron.com>
*
*     12/15/90 - initial hack  
*       8/7/91 - clean-up time, first real version 
*      1/15/92 - add inter-buffer delay option
*      1/16/92 - clean up echo mode code, add #ifdef for AIX
*/

#include <netdb.h>
#include <stdio.h>

#include <malloc.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>

#define DEFBLKSIZE 1024		/* default  blocksize is 1k */
#define DEFNBLKS 100		/* default number of blocks is 100 */

extern char *optarg;		/* external vars used by getopt(3) */
extern int optind, opterr;

/*
* neat macro from ka9q to "do the right thing" with ansi prototypes
*/

#ifndef __ARGS
#ifdef __STDC__
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#endif
#endif

static void usage  __ARGS((char *argv[]));		/* forward declaration */


#if !defined(sun) && !defined(_AIX)	/* sunos and aix define pid_t, */
/* do others? */ 
typedef int pid_t;
#endif

#ifndef TRUE
#define TRUE (1)
#endif /*TRUE*/

#ifndef FALSE
#define FALSE (0)
#endif /*FALSE*/

int
main(argc, argv)
unsigned int argc;
char *argv[];
{
	
	int eflag = FALSE;	       /* use echo instead of discard for full duplex*/
	int vflag = FALSE;	        /* verbosity*/
	int fflag = FALSE;		/* preload buffer with file */
	int delay = 0;		/* inter-buffer delay in usecs */
	pid_t pid = 0;		/* pid of receiver child */
	int status;			/* return status of child */
	
	struct timeval start, end;	/* used to store start and end time of I/O */
	
#ifndef sun
	struct timeval timeout;	/* used for timeout in select call */
#endif /*sun*/
	
	double delta;			/* stores delta of start and end in sec  */
	
	/* generic counter */
	register int cnt;
	
	int c;		/* used to return options from getopt(3) */
	
	struct sockaddr_in sin;	/* sockaddr for socket */
	struct hostent *hp;		/* hostent for host name lookups */
	struct servent *serv;		/* service entry for port lookup */
	
	int sock;			/* socket descriptor */
	int fd[2];			/* descriptors for pipe */
	
	unsigned int blksize = DEFBLKSIZE; /* block size (1k default) */
	unsigned int nblks = DEFNBLKS;	/* number of blocks (100 default)*/
	unsigned int nbytes;		/* number of bytes to transfer */
	register unsigned int bytes_left;	/* keep track of bytes left to */
	/* read/write */ 
	register char *buf;		/* input and output buffer (malloced) */
	register char *bufp;		/* placeholder pointer */
	
	FILE *infile;			/* used to preload buffer */
	
	while ((c = getopt(argc, argv, "vehb:n:f:d:")) != -1) {
		switch (c) {
		case 'v':
			vflag = TRUE;
			break;
			
		case 'e':
			eflag = TRUE;
			break;
			
		case 'h':
			usage(argv);
			break;
			
		case 'n':
			nblks = atoi(optarg);
			break;
			
		case 'b':
			blksize = atoi(optarg);
			break;
			
		case 'f':
			fflag = TRUE;
			if ((infile = fopen(optarg, "r")) == NULL) {
				fprintf(stderr, "%s: Can't open file %s.\n", argv[0], optarg);
				exit(2);
			}
			break;
			
		case 'd':
			delay = atoi(optarg);
#ifndef sun
			/* we need to fake usleep() with select() on non-sun machines */
			
			timeout.tv_usec = atoi(optarg) ;
			timeout.tv_sec = 0;
#endif /*sun*/
			
			break;
			
		default:
			usage(argv);
			break;
		}
	}
	
	if ((argc - optind) != 1)	/* we better have a host name */
		usage(argv);
    
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)  {
		perror("socket");
		exit(1);
	}
	
	bzero((char *) &sin, sizeof(sin));
	sin.sin_family = AF_INET;
	
	if (bind(sock, &sin, sizeof (sin)) == -1) {
		perror("bind");
		exit(1);
	}
	
	hp = gethostbyname(argv[optind]);
	
	if (hp) {           /* try name first */
		sin.sin_family = hp->h_addrtype;
		bcopy(hp->h_addr, &sin.sin_addr, hp->h_length);
	}
	
	else {			/* maybe it's a numeric address ?*/
		sin.sin_family = AF_INET;
		
		if ((sin.sin_addr.s_addr = inet_addr(argv[optind])) == -1)  { 
			/* nope, sorry we lose */
			fprintf(stderr, "host not found: %s\n", argv[optind]);
			exit(1);
		}
		
	}
	
	serv = getservbyname(eflag ? "echo" : "discard", "tcp"); /* get port */
	sin.sin_port = serv->s_port;
	
	if (connect(sock, &sin, sizeof(sin)) == -1) {
		perror("connect");
		exit(1);
	}
	
	nbytes = nblks * blksize;	/* number of bytes to send/receive */ 
	
	if (vflag)
		printf("Sending %d bytes with blocksize %d bytes\n",
		nbytes, blksize); 
    
	if ((buf =  malloc((blksize < 4096) ? 4096 : blksize)) == NULL) {
		perror("malloc buf");
		exit(1);
	}
	
	bzero(buf, blksize);		/* clean out the buffer */
	
	if (fflag) {			/* preload buffer with file */
		cnt = fread(buf, sizeof(u_char), blksize, infile);
		if (cnt < blksize)
			fprintf(stderr, "Warning! File is smaller than blocksize.\n");
	}
	
	
	if (eflag) {
		if (pipe(fd) == -1) {	/* create pipe for return */
			perror("pipe");
			exit(1);
		}
		
		if ((pid = fork()) == -1) { /* fork off a receiver */
			perror("fork()");
			exit(1);
		}
	}
	if (eflag && pid != 0) {			/* we are the receiver */
		close (fd[1]);
		bytes_left = nblks * blksize;
		
		if (gettimeofday(&start, NULL) == -1) {
			perror("gettimeofday");
			exit(1);
		}
		if (vflag) {
			register int flush_flag = FALSE, blk_boundry = bytes_left - blksize;
			
			while (bytes_left) {
				if ((cnt = read(sock, buf, bytes_left)) == -1)  {
					perror("receive:");
					exit(2);
				}
				
				bytes_left -= cnt;
				
				while (blk_boundry > bytes_left) {
					fputs("\b \b", stdout);
					flush_flag = TRUE;
					blk_boundry -= blksize;
				}
				if (flush_flag) {
					fflush(stdout);
					flush_flag = FALSE;
				}
			}
			if (gettimeofday(&end, NULL) == -1) {
				perror("gettimeofday");
				exit(1);
			}
			printf("\b");
			fflush(stdout);
		}
		else {			/* not vflag */
			while (bytes_left) {
				
				if ((cnt = read(sock, buf, bytes_left)) == -1)  {
					perror("receive:");
					exit(2);
				}
				bytes_left -= cnt;
				
			}
			if (gettimeofday(&end, NULL) == -1) {
				perror("gettimeofday");
				_exit(1);
			}
		}
		
		delta = (double) (end.tv_sec - start.tv_sec) + (double)
			((double) (end.tv_usec - start.tv_usec) / 1000000.0);
		printf("Received %d bytes in %f seconds (%0.3f kbytes/s)\n",
			nbytes, delta, (double) (nbytes / delta) /1024);
		
		if (wait(&status) == -1) {/* wait for child lest we */
			/* unleash zombies */ 
			perror("wait");
			exit(1);
		}
		
		if (status) {
			fprintf(stderr, "child returned non-zero status %d\n", status);
		}
		/* get delta from transmitter process */
		if ((cnt = read(fd[0], (char *) &delta, sizeof(delta))) == -1) {
			perror("read pipe");
			exit(1);
		}
		printf("Transmitted %d bytes in %f seconds (%0.3f kbytes/s)\n",
			nbytes, delta, (double) (nbytes / delta) / 1024); 
		
	}
	else {			/* we are the transmitter */
		
		if (vflag) {
			if (gettimeofday(&start, NULL) == -1) {
				perror("gettimeofday");
				exit(1);
			}
			
			while (nblks--) {
				cnt = 0;
				bufp = buf;
				bytes_left = blksize;
				do {
					if ((cnt = write(sock, bufp, bytes_left)) == -1)  {
						perror("receive:");
						exit(2);
					}
					bufp += cnt;
					bytes_left -= cnt;
					
#ifdef sun
					if (delay)
						usleep(delay);
#else /*sun*/
					if (delay)
						select(0, NULL, NULL, NULL, timeout);
#endif /*sun*/
					
				} while (bytes_left);
				putchar('.');
				fflush(stdout);
			}
			
			if (gettimeofday(&end, NULL) == -1) {
				perror("gettimeofday");
				exit(1);
			}
		}
		else {			/* not vflag */
			if (gettimeofday(&start, NULL) == -1) {
				perror("gettimeofday");
				exit(1);
			}
			
			while (nblks--) {
				cnt = 0;
				bufp = buf;
				bytes_left = blksize;
				do {
					if ((cnt = write(sock, bufp, bytes_left)) == -1)  {
						perror("receive:");
						exit(2);
					}
					bufp += cnt;
					bytes_left -= cnt;
					
#ifdef sun
					if (delay)
						usleep(delay);
#else /*sun*/
					if (delay)
						select(0, NULL, NULL, NULL, timeout);
#endif /*sun*/
					
				} while (bytes_left);
			}
			
			if (gettimeofday(&end, NULL) == -1) {
				perror("gettimeofday");
				exit(1);
			}
		}
		delta = (double) (end.tv_sec - start.tv_sec) + (double)
			((double) (end.tv_usec - start.tv_usec) / 1000000.0);
		
		/* write delta value to pipe */
		
		if (eflag) {
			if ((cnt = write(fd[1], (char *) &delta, sizeof(delta))) == -1) {
				perror("write pipe");
				exit(1);
			}
			exit(0);
		}
		else
			
			printf("\nTransmitted %d bytes in %f seconds (%0.3f kbytes/s)\n",
			nbytes, delta, (double) (nbytes / delta) / 1024); 
		
	}
	
	exit(0);
}


static void
usage(argv)
char *argv[];
{
	fprintf(stderr, "usage: %s [-v] [-e] [-h] [-b blksize] [-n nblks] [-f file] host\n", argv[0]);
	fprintf(stderr, "      -v verbose\n");
	fprintf(stderr, "      -e use echo service for full duplex transfer\n");
	fprintf(stderr, "      -h print this message\n");
	fprintf(stderr, "      -b blocksize in bytes (default 1024)\n");
	fprintf(stderr, "      -n number of blocks (default 100)\n");
	fprintf(stderr, "      -f file to preload buffer (zero buffer by default)\n");
	fprintf(stderr, "      -d inter-buffer transmission delay in usecs (default 0)\n");
	exit(1);
}
