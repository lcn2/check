# $Id: Makefile,v 1.8 1996/03/06 17:12:59 chongo Exp $ 

SHELL = /bin/sh
DEST = /usr/local/bin
RM = /bin/rm -f
CP = /bin/cp
CHMOD = /bin/chmod

TARGETS = check rsync

all: ${TARGETS}

check: check.c
	${CC} ${CFLAGS} -o check check.c

rsync: rsync.c
	${CC} ${CFLAGS} -o rsync rsync.c

install: all
	@for i in ${TARGETS}; do \
	    echo "${RM} ${DEST}/$$i"; \
	    ${RM} ${DEST}/$$i; \
	    echo "${CP} $$i ${DEST}"; \
	    ${CP} $$i ${DEST}; \
	    echo "${CHMOD} 0555 ${DEST}/$$i"; \
	    ${CHMOD} 0555 ${DEST}/$$i; \
	done

clean:
	${RM} *.o

clobber: clean
	${RM} ${TARGETS}
