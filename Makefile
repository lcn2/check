#!/usr/bin/make
#
# check - check for checked out RCS files
#
# @(#) $Revision: 1.3 $
# @(#) $Id: Makefile,v 1.3 1999/09/22 01:37:47 chongo Exp chongo $
# @(#) $Source: /usr/local/src/cmd/check/RCS/Makefile,v $
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
DEST = /usr/local/bin
RM = /bin/rm -f
CP = /bin/cp
CHMOD = /bin/chmod
CC = cc
CFLAGS = -O2

TARGETS = check rcheck

all: ${TARGETS}

check: check.c
	${CC} ${CFLAGS} -o check check.c

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
	${RM} check
