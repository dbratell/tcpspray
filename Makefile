CFLAGS= -O2 -s -Dsun
BINDIR= /usr/local/etc
INSTALL= install -c
CC= gcc

all: tcpspray

tcpspray:	tcpspray.c
	${CC} ${CFLAGS} tcpspray.c -o tcpspray

install: tcpspray
	${INSTALL} tcpspray ${BINDIR}
clean:
	rm -f tcpspray *.o *~ #* core


saber:
	#setopt load_flags $(CFLAGS)
	#load tcpspray.c
