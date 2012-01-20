/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*******************************************************************************/

#include <sys/types.h>
#include <sys/event.h>

#include "sys/inotify.h"
#include "conversions.h"

/* It is just a shortctut */
static const int NOTE_MODIFIED = (NOTE_WRITE | NOTE_EXTEND);

/**
 * Convert the inotify watch mask to the kqueue event filter flags.
 *
 * @param[in] flags        An inotify watch mask.
 * @param[in] is_directory 1 for directories, 0 for files.
 * @return Converted kqueue event filter flags.
 **/  
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


/**
 * Convert the kqueue event filter flags to the inotify watch mask. 
 *
 * @param[in] flags        A kqueue filter flags.
 * @param[in] is_directory 1 for directories, 0 for files.
 * @return Converted inotify watch mask.
 **/  
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

    if ((result & (IN_ATTRIB | IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE))
        && is_directory) {
        result |= IN_ISDIR;
    }

    return result;
}
