/* WinTCPSpray 
 *
 * Daniel Bratell
 *
 * v1.2 1998 2 May
 *    Sends a random stream of bytes instead of only
 *    zeros.
 *
 * v1.1 1998 7 April
 *    Made the program handle breaks (CTRL-C) in a
 *    nicer way buy printing the info gathered so far.
 *
 * v1.01a 1997 5 November
 *    Made the program independent of ws2_32.dll. 
 *    WSock32.DLL used instead.
 * 
 * v1.01 1997 12 October 
 *    Inserted check for too large amounts of data to
 *    transfer.
 *
 * v1.0 1997 6 October 
 *    First release
 *  
 */

#include <WINSOCK.H>
#include <stdio.h>
#include <time.h>
#include <windows.h>

#define VERSIONSTRING "TCPSpray for Windows version 1.2"
#define	COPYRIGHTSTRING "(c) 1997,1998 Daniel Bratell (bratell@lysator.liu.se)"

#define DEFBLKSIZE 1024		/* default  blocksize is 1k */
#define DEFNBLKS 100		/* default number of blocks is 100 */
#define MAXTRANSFER (10*1024*1024) /* Maximum nuber of bytes in a send */

#define MAX_TIME_ERROR	(0.01) /* The maximum error in time measurement */


#define TRUE 1
#define FALSE 0

void ProcessArguments(int argc, char *argv[]);
static void usage(char *argv[]);		/* forward declaration */
static void ReportErrorAndExit(int WSAError);		/* forward declaration */
BOOL breakhandler(DWORD fdwCtrlType); 

unsigned int blksize = DEFBLKSIZE; /* block size (1k default) */
unsigned int nblks = DEFNBLKS;	/* number of blocks (100 default)*/
int verbose = 0; /* Don't talk */
int go_on = TRUE; /* Let's go. It's set by the break-handler. */

char *targethost;

int main(int argc, char *argv[])
{

	int start, end;	/* Stores start and endtime of measurement */
	double delta;			/* stores delta of start and end in sec  */
	
	/* generic counter */
	int cnt;
	unsigned int i;

	WSADATA wsaData;

	struct sockaddr_in sin;	/* sockaddr for socket */
	struct sockaddr *sinpek; /* Pointer to the previous sockaddr_in */
	struct hostent *hp;		/* hostent for host name lookups */
	struct servent *serv;		/* service entry for port lookup */
	
	int sock;			/* socket descriptor */
	
	unsigned int nbytes;		/* number of bytes to transfer */
	unsigned int bytes_left;	/* keep track of bytes left to */
	/* read/write */ 
	char *buf;		/* input and output buffer (malloced) */
	char *bufp;		/* placeholder pointer */

	BOOL fSuccess;

	/* Set handler for ctrl-C. */
	fSuccess = SetConsoleCtrlHandler( 
	    (PHANDLER_ROUTINE) breakhandler,  /* handler function */ 
		TRUE);                           /* add to list      */ 
	if (! fSuccess) {
		/* Too bad. But since things work anyway, I don't
		 * really care.
		 */
    }

	/* Look at the arguments and set the variables
	 * accordingly
	 */
	ProcessArguments(argc, argv);

	/* We don't want to clog up the net. */
	if(nblks*blksize>MAXTRANSFER) {
		fprintf(stderr, "Too much data to transfer! May clog network.\n");
		exit(1);
	}

	if(WSAStartup(0x101,&wsaData)) {
		perror("WSAStartup");
		exit(1);
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == 0)  {
		fprintf(stderr, "Error: %s ", WSAGetLastError());
		perror("WSASocket");
		WSACleanup();
		exit(1);
	}

	sinpek = (struct sockaddr *)&sin;

	memset((char *) &sin, (char) 0,  sizeof(sin));
	sin.sin_family = AF_INET;
	
	hp = gethostbyname(targethost);

	if (hp) {           /* try name first */
		sin.sin_family = hp->h_addrtype;
		memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
	} else {
		sin.sin_family = AF_INET;
		
		if ((sin.sin_addr.s_addr = inet_addr(targethost)) == -1)  { 			
			/* nope, sorry we lose */
			ReportErrorAndExit(WSAGetLastError());
			fprintf(stderr, "host not found: %s\n", targethost);
			WSACleanup();
			exit(1);
		}
	}

	serv = getservbyname("discard", "tcp"); /* get port */
	sin.sin_port = serv->s_port;
	
	if (connect(sock, sinpek, sizeof(sin))) {
		ReportErrorAndExit(WSAGetLastError());
	}

	nbytes = nblks * blksize;	/* number of bytes to send/receive */ 

	if(verbose)
		printf("Sending %d bytes with blocksize %d bytes\n",
		nbytes, blksize); 

	if ((buf =  malloc(blksize)) == NULL) {
		perror("malloc buf");
		WSACleanup();
		exit(1);
	}
		/* clean out the buffer */
/*	memset(buf, (char) 0, blksize);
*/
	
	/* Fill the buffer with random data. */
	/* Could be done more efficient by setting
	 * bigger blocks than one byte at a time to
	 * a random value. 
     */
	srand((unsigned)time(NULL));
	for(i = 0; i<blksize; i++) {
		buf[i] = rand();
	}
	
	start = GetTickCount();

	while (nblks-- && go_on) {
		cnt = 0;
		bufp = buf;
		bytes_left = blksize;
		do {
			if ((cnt = send(sock, bufp, bytes_left,0)) == -1)  {
				perror("send:");
				fprintf(stderr, "Error: %s ", WSAGetLastError());
				WSACleanup();
				exit(2);
			}
			bufp += cnt;
			bytes_left -= cnt;
			
		} while (bytes_left);
		if(verbose) {
			putchar('.');
		}
	}
	fflush(stdout);
	end=GetTickCount();
	
	delta = ((double) (end-start)/1000.0);


	/* Calculate and print the performance taking into account
	 * that the sending may have been prematurely
	 * interrupted 
	 */
	nbytes -= nblks*blksize;
	if(delta > MAX_TIME_ERROR) {
		printf("\nTransmitted %d bytes in %0.3f seconds (%0.3f±%0.3f kbytes/s)\n",
			nbytes, delta, (double) (nbytes / delta) / 1024,
			((double)nbytes*MAX_TIME_ERROR)/(delta*(delta-MAX_TIME_ERROR)*1024)); 
	} else {
		printf("\nTransmitted %d bytes which wasn't enough to get a speed reading.\n",
			nbytes);
		printf("(Min throughput: %0.3f kbytes/s)\n",
				(double) (nbytes / (2*MAX_TIME_ERROR)) / 1024); 
	}
	
	closesocket(sock);
	WSACleanup();

	return 0;
}

static void usage(char *argv[])
{
	fprintf(stderr, "usage: %s [-v] [-h] [-b blksize] [-n nblks] host\n", argv[0]);
	fprintf(stderr, "      -v verbose\n");
/*	fprintf(stderr, "      -e use echo service for full duplex transfer\n");
*/	fprintf(stderr, "      -h print this message\n");
	fprintf(stderr, "      -b blocksize in bytes (default 1024)\n");
	fprintf(stderr, "      -n number of blocks (default %d)\n", DEFNBLKS);
/*	fprintf(stderr, "      -f file to preload buffer (zero buffer by default)\n");
	fprintf(stderr, "      -d inter-buffer transmission delay in usecs (default 0)\n");
*/
	fprintf(stderr, "%s\n", VERSIONSTRING);
	fprintf(stderr, "%s\n", COPYRIGHTSTRING);
	exit(1);
}

void ReportErrorAndExit(int WSAError) 
{
	char *ps;

	switch(WSAError) {
	case WSAETIMEDOUT:
		ps="Connection timed out.";
		break;
	case WSAECONNREFUSED:
		ps="Connection refused.";
		break;
	case WSAHOST_NOT_FOUND:
		ps="Host not found.";
		break;
	default:
		fprintf(stderr, "%d\n", WSAError);
		ps="Unknown error.";
		break;
	}

	fprintf(stderr, "%s: %s\n", targethost, ps);
	WSACleanup();
	exit(1);
}


void ProcessArguments(int argc, char *argv[])
{
	int currentarg = 1;
	targethost = NULL;

	while(currentarg<argc) {
		if((argv[currentarg][0]=='-') && 
			(argv[currentarg][1]!='\0') && 
			(argv[currentarg][2]=='\0')) {
			/* Command line option */
			switch(argv[currentarg][1]) {
			case 'n':
				// Number of blocks
				if(argc < currentarg+1) {
					// No argument
					usage(argv);
				}
				nblks = atoi(argv[currentarg+1]);
				currentarg++;
				break;
				
			case 'b':
				// Blocksize
				if(argc < currentarg+1) {
					// No argument
					usage(argv);
				}
				blksize = atoi(argv[currentarg+1]);
				currentarg++;
				break;

			case 'v':
				// Verbose
				verbose = 1;
				break;

			case 'h':
				// Help
				usage(argv);
				break;

			default:
				// Unregognized option
				fprintf(stderr, "Unrecognized option %s.", argv[currentarg]);
				exit(1);
			}
		} else {
			/* Unregognized option, might be 
			   more than one target computer
		     */
			if(currentarg == argc-1) {
				// The host
				targethost = argv[currentarg];
			} else {
				// Strange option!
				usage(argv);
			}
		}

		currentarg++;
	}

	if(targethost==NULL) /* we better have a host name */
		usage(argv);

}

/* Handler for CTRL-C events */
BOOL breakhandler(DWORD fdwCtrlType)
{
	if(fdwCtrlType == CTRL_C_EVENT) {
		go_on = FALSE;
		return TRUE;
	}

	/* It wasn't a CTRL-C so tell the system
	 * we couldn't handle it.
	 */
	return FALSE;
}	

