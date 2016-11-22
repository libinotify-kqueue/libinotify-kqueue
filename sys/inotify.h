#ifndef __BSD_INOTIFY_H__
#define __BSD_INOTIFY_H__

#include <stdint.h>

#ifndef __THROW
  #ifdef __cplusplus
    #define __THROW throw()
  #else
    #define __THROW
  #endif
#endif

/*
 * Libinotify-specific: Default size of communication socket buffer in bytes.
 * This defines a recommended read(2) buffer size for libinotify event
 * consumers. Lower values can cause partial event reads. Bigger values
 * is just a wasting of memory.
 * The value is arbitrary, has been acquired from code sample in linux
 * inotify(7) man page and seems to be very common among the inotify clients.
 */
#define IN_SOCKBUFSIZE			0
#define	IN_DEF_SOCKBUFSIZE		4096
/* linux`s /proc/sys/fs/inotify/max_queued_events counterpart */
#define IN_MAX_QUEUED_EVENTS		1
#define IN_DEF_MAX_QUEUED_EVENTS	16384

/* Flags for the parameter of inotify_init1. */
#define IN_CLOEXEC	02000000	/* Linux x86 O_CLOEXEC */
#define IN_NONBLOCK	00004000	/* Linux x86 O_NONBLOCK */


/* Structure describing an inotify event. */
struct inotify_event
{
    int wd;          /* Watch descriptor.  */
    uint32_t mask;   /* Watch mask.  */
    uint32_t cookie; /* Cookie to synchronize two events.  */
    uint32_t len;    /* Length (including NULLs) of name.  */
    char name[];     /* Name.  */
};


/* Supported events suitable for MASK parameter of INOTIFY_ADD_WATCH.  */
#define IN_ACCESS        0x00000001 /* File was accessed.  */
#define IN_MODIFY        0x00000002 /* File was modified.  */
#define IN_ATTRIB        0x00000004 /* Metadata changed.  */
#define IN_CLOSE_WRITE   0x00000008 /* Writtable file was closed.  */
#define IN_CLOSE_NOWRITE 0x00000010 /* Unwrittable file closed.  */
#define IN_CLOSE         (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* Close.  */
#define IN_OPEN          0x00000020 /* File was opened.  */
#define IN_MOVED_FROM    0x00000040 /* File was moved from X.  */
#define IN_MOVED_TO      0x00000080 /* File was moved to Y.  */
#define IN_MOVE          (IN_MOVED_FROM | IN_MOVED_TO) /* Moves.  */
#define IN_CREATE        0x00000100 /* Subfile was created.  */
#define IN_DELETE        0x00000200 /* Subfile was deleted.  */
#define IN_DELETE_SELF   0x00000400 /* Self was deleted.  */
#define IN_MOVE_SELF     0x00000800 /* Self was moved.  */


/* Additional events and flags. Some of these flags are unsupported,
   but still should be present */
#define IN_UNMOUNT	 0x00002000	/* Backing fs was unmounted.  */
#define IN_Q_OVERFLOW	 0x00004000	/* Event queued overflowed.  */
#define IN_IGNORED	 0x00008000	/* File was ignored.  */

#define IN_ONLYDIR	 0x01000000	/* Only watch the path if it is a
					   directory.  */
#define IN_DONT_FOLLOW	 0x02000000	/* Do not follow a sym link.  */
#define IN_EXCL_UNLINK	 0x04000000	/* Exclude events on unlinked
					   objects.  */
#define IN_MASK_ADD	 0x20000000	/* Add to the mask of an already
					   existing watch.  */
#define IN_ISDIR	 0x40000000	/* Event occurred against dir.  */
#define IN_ONESHOT	 0x80000000	/* Only send event once.  */


/*
 * All of the events - we build the list by hand so that we can add flags in
 * the future and not break backward compatibility.  Apps will get only the
 * events that they originally wanted.  Be sure to add new events here!
 */
#define IN_ALL_EVENTS	(IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | \
			 IN_CLOSE_NOWRITE | IN_OPEN | IN_MOVED_FROM | IN_MOVE_SELF | \
			 IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF)

__BEGIN_DECLS

/* Create and initialize inotify-kqueue instance. */
int inotify_init (void) __THROW;

/* Create and initialize inotify-kqueue instance. */
int inotify_init1 (int flags) __THROW;

/* Add watch of object NAME to inotify-kqueue instance FD. Notify about
   events specified by MASK. */
int inotify_add_watch (int fd, const char *name, uint32_t mask) __THROW;

/* Remove the watch specified by WD from the inotify instance FD. */
int inotify_rm_watch (int fd, int wd) __THROW;

/* Libinotify specific. Set inotify instance parameter. */
int inotify_set_param (int fd, int param, intptr_t value) __THROW;

__END_DECLS

#endif /* __BSD_INOTIFY_H__ */
