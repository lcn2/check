#!/usr/bin/env make
#
# check - check for checked out RCS files
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

SHELL= bash
DESTBIN= /usr/local/bin
RM= rm
CP= cp
CHMOD= chmod
CC= cc
CFLAGS= -O3 -g3 -Wall -W
INSTALL= install


TARGETS = check rcheck

all: ${TARGETS}

check: check.c
	${CC} ${CFLAGS} -o check check.c

rcheck: check
	@${RM} -f $@
	${CP} -p -f $? $@

install: all
	${INSTALL} -m 0555 ${TARGETS} ${DESTBIN}

clean:
	${RM} -f *.o

clobber: clean
	${RM} -f check rcheck

# help
#
help:
	@echo make all
	@echo make install
	@echo make clean
	@echo make clobber
