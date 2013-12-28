#!/usr/bin/make
#
# check - check for checked out RCS files
#
# @(#) $Revision: 3.4 $
# @(#) $Id: Makefile,v 3.4 2013/12/28 15:50:51 chongo Exp chongo $
# @(#) $Source: /usr/local/src/bin/check/RCS/Makefile,v $
#
# Please do not copyright this code.  This code is in the public domain.
#
# LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
# EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
# USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

SHELL = /bin/sh
DESTBIN = /usr/local/bin
RM = rm
CP = cp
CHMOD = chmod
CC = cc
CFLAGS = -O3 -g3 -Wall -W

TARGETS = check rcheck

# remote operations
#
THISDIR= check
RSRCPSH= rsrcpush
RMAKE= rmake

all: ${TARGETS}

check: check.c
	${CC} ${CFLAGS} -o check check.c

rcheck: check
	@${RM} -f $@
	${CP} -p -f $? $@

install: all
	@for i in ${TARGETS}; do \
	    echo "${RM} -f ${DESTBIN}/$$i"; \
	    ${RM} -f "${DESTBIN}/$$i"; \
	    echo "${CP} -f $$i ${DESTBIN}"; \
	    ${CP} -f "$$i" "${DESTBIN}"; \
	    echo "${CHMOD} 0555 ${DESTBIN}/$$i"; \
	    ${CHMOD} 0555 "${DESTBIN}/$$i"; \
	done

clean:
	${RM} -f *.o

clobber: clean
	${RM} -f check rcheck

# push source to remote sites
#
pushsrc: all
	${RSRCPSH} -v -x . ${THISDIR}

pushsrcq: all
	@${RSRCPSH} -q . ${THISDIR}

pushsrcn: all
	${RSRCPSH} -v -x -n . ${THISDIR}

# run make on remote hosts
#
rmtall:
	${RMAKE} ${THISDIR} all

rmtinstall:
	${RMAKE} ${THISDIR} install

rmtclean:
	${RMAKE} ${THISDIR} clean

rmtclobber:
	${RMAKE} ${THISDIR} clobber
