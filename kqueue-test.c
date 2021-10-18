#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <stdio.h>
#include <fcntl.h>
#include <err.h>

#ifndef NOTE_READ
#define NOTE_READ 0
#endif
#ifndef NOTE_OPEN
#define NOTE_OPEN 0
#endif
#ifndef NOTE_CLOSE
#define NOTE_CLOSE 0
#endif
#ifndef NOTE_CLOSE_WRITE
#define NOTE_CLOSE_WRITE 0
#endif
int
main(int argc, char *argv[])
{
	int fd, kq, nev, openflags = O_NONBLOCK;
	struct kevent ev;
	static const struct timespec tout = { 1, 0 };

#ifdef O_PATH
	openflags |= O_PATH;
#elif defined(O_EVTONLY)
	openflags |= O_EVTONLY;
#else
	openflags |= O_RDONLY;
#endif
#if defined(O_SYMLINK) && !defined(O_PATH)
	openflags |= O_SYMLINK;
#else
	openflags |= O_NOFOLLOW;
#endif
	if ((fd = open(argv[1], openflags)) == -1)
		err(1, "Cannot open `%s'", argv[1]);

	if ((kq = kqueue()) == -1)
		err(1, "Cannot create kqueue");

	EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	    NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND|NOTE_ATTRIB|NOTE_LINK|
	    NOTE_RENAME|NOTE_REVOKE|NOTE_READ|NOTE_OPEN|NOTE_CLOSE|
	    NOTE_CLOSE_WRITE, 0, 0);
	if (kevent(kq, &ev, 1, NULL, 0, &tout) == -1)
		err(1, "kevent");
	for (;;) {
		nev = kevent(kq, NULL, 0, &ev, 1, &tout);
		if (nev == -1)
			err(1, "kevent");
		if (nev == 0)
			continue;
		if (ev.fflags & NOTE_DELETE) {
			printf("deleted ");
			ev.fflags &= ~NOTE_DELETE;
		}
		if (ev.fflags & NOTE_WRITE) {
			printf("written ");
			ev.fflags &= ~NOTE_WRITE;
		}
		if (ev.fflags & NOTE_EXTEND) {
			printf("extended ");
			ev.fflags &= ~NOTE_EXTEND;
		}
		if (ev.fflags & NOTE_ATTRIB) {
			printf("chmod/chown/utimes ");
			ev.fflags &= ~NOTE_ATTRIB;
		}
		if (ev.fflags & NOTE_LINK) {
			printf("hardlinked ");
			ev.fflags &= ~NOTE_LINK;
		}
		if (ev.fflags & NOTE_RENAME) {
			printf("renamed ");
			ev.fflags &= ~NOTE_RENAME;
		}
		if (ev.fflags & NOTE_REVOKE) {
			printf("revoked ");
			ev.fflags &= ~NOTE_REVOKE;
		}
		if (ev.fflags & NOTE_READ) {
			printf("accessed ");
			ev.fflags &= ~NOTE_READ;
		}
		if (ev.fflags & NOTE_OPEN) {
			printf("opened ");
			ev.fflags &= ~NOTE_OPEN;
		}
		if (ev.fflags & NOTE_CLOSE) {
			printf("closed not for write");
			ev.fflags &= ~NOTE_CLOSE;
		}
		if (ev.fflags & NOTE_CLOSE_WRITE) {
			printf("closed for write");
			ev.fflags &= ~NOTE_CLOSE_WRITE;
		}
		printf("\n");
		if (ev.fflags)
			warnx("unknown event 0x%x\n", ev.fflags);
	}
}
