CC = gcc
PROG = nebulafm
MANNAME = $(PROG).1
SRCMODULES = nebulafm.c
OBJMODULES = $(SRCMODULES: .c = .o)

SOURCE_CFLAGS = -D_GNU_SOURCE
CURSES_CFLAGS = `pkg-config --cflags ncursesw`
MAGIC_LIBS = -lmagic
CURSES_LIBS = `pkg-config --libs ncursesw`

CFLAGS = $(SOURCE_CFLAGS) $(CURSES_CFLAGS)
LIBS = $(MAGIC_LIBS) $(CURSES_LIBS)

BINPREFIX = /usr/bin
MANPREFIX = /usr/share/man

all: $(OBJMODULES)
	$(CC) $(CFLAGS) $(OBJMODULES) -o $(PROG) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $<

install:
	install -Dm 755 $(PROG) $(BINPREFIX)/$(PROG)
	install -Dm 644 $(MANNAME) $(MANPREFIX)/man1/$(MANNAME)

uninstall:
	rm -v $(BINPREFIX)/$(PROG)
	rm -v $(MANPREFIX)/man1/$(MANNAME)

clean:
	rm *~
	rm *.o
