# Makefile for mwm - see LICENSE for license and copyright information

WMNAME  = mwm
VERSION = 0.1
PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
MANPREFIX = ${PREFIX}/share/man
X11INC = -I /usr/X11R6/include \
  -I /usr/include \
  -I /usr/local/include \
  -I /usr/lib/dbus-1.0/include \
  -I /usr/local/lib/dbus-1.0/include \
  -I /usr/include/dbus-1.0 \
  -I /usr/local/include/dbus-1.0 \
  -I .
X11LIB = -L /usr/X11R6/lib -L /usr/lib -L /usr/local/lib 
LIBS = -l c -l X11 -l Xinerama -l dbus-1
INCS = ${X11INC}
CFLAGS   = -std=c99 -fPIE -fPIC -pedantic -Wall -Wextra ${INCS} -DVERSION=\"${VERSION}\"
LDFLAGS  = ${X11LIB} ${LIBS}
CC 	 = cc
SRC  = ${WMNAME}.c dbus.c
OBJ  = ${SRC:.c=.o}

all: ${WMNAME}

dbg: ${WMNAME}_dbg

options:
	@echo ${WMNAME} build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

${OBJ}: config.h

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

${WMNAME}: $(OBJ)
	@echo CC -c $(CFLAGS) -O3 -o $@
	@${CC} $(CFLAGS) -O3 -o $@ ${OBJ} ${LDFLAGS} -s

${WMNAME}_dbg: $(OBJ)
	@echo CC -c $(CFLAGS) -O0 -g -o $@
	@${CC} $(CFLAGS) -O0 -g -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -fv ${WMNAME} $(WMNAME)_dbg ${OBJ} *.core

install: all
	@echo installing executable file(s) to ${DESTDIR}${PREFIX}/bin
	@install -Dm755 ${WMNAME} ${DESTDIR}${PREFIX}/bin/${WMNAME}
	@install -Dm755 ${WMNAME}_dbg ${DESTDIR}${PREFIX}/bin/${WMNAME}
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man.1
	@install -Dm644 ${WMNAME}.1 ${DESTDIR}${MANPREFIX}/man1/${WMNAME}.1

uninstall:
	@echo removing executable file(s) from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/${WMNAME} ${DESTDIR}${PREFIX}/bin/${WMNAME}_dbg
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/${WMNAME}.1

.PHONY: all dbg options clean install uninstall
