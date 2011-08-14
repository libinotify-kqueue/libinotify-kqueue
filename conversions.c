#include <sys/event.h>

#include "inotify.h"
#include "conversions.h"

/* It is just a shortctut */
#define NOTE_MODIFIED (NOTE_WRITE | NOTE_EXTEND)

uint32_t
inotify_to_kqueue (uint32_t flags, int is_directory)
{
    uint32_t result = 0;

    if (flags & IN_ATTRIB)
        result |= (NOTE_ATTRIB | NOTE_LINK);
    if (flags & IN_MODIFY)
        result |= NOTE_MODIFIED;
    if (flags & IN_MOVED_FROM && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_MOVED_TO && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_CREATE && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_DELETE && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_DELETE_SELF)
        result |= NOTE_DELETE;
    if (flags & IN_MOVE_SELF)
        result |= NOTE_RENAME;

    return result;
}


uint32_t
kqueue_to_inotify (uint32_t flags, int is_directory)
{
    uint32_t result = 0;

    if (flags & (NOTE_ATTRIB | NOTE_LINK))
        result |= IN_ATTRIB;

    if ((flags & NOTE_MODIFIED)
        && is_directory == 0)
        result |= IN_MODIFY;

    if (flags & NOTE_DELETE)
        result |= IN_DELETE_SELF;

    if (flags & NOTE_RENAME)
        result |= IN_MOVE_SELF;

    return result;
}
