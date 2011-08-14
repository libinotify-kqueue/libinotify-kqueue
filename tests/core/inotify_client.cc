#include <cassert>
#include <cstring>
#include <poll.h>
#include "inotify_client.hh"
#include "log.hh"

inotify_client::inotify_client ()
: fd (inotify_init())
{
    assert (fd != -1);
}

inotify_client::~inotify_client ()
{
    // close (fd);
}

int inotify_client::watch (const std::string &filename, uint32_t flags)
{
    assert (fd != -1);
    LOG ("INO: Adding " << VAR (filename) << VAR (flags));

    int retval = inotify_add_watch (fd, filename.c_str(), flags);
    LOG ("INO: " << VAR (retval));
    return retval;
}

void inotify_client::cancel (int watch_id)
{
    assert (fd != -1);
    if (inotify_rm_watch (fd, watch_id) != 0) {
        LOG ("INO: rm watch failed " << VAR (fd) << VAR (watch_id));
    }
}

#define IE_BUFSIZE ((sizeof (struct inotify_event) + FILENAME_MAX))

bool inotify_client::get_next_event (event& ev, int timeout) const
{
    struct pollfd pfd;
    memset (&pfd, 0, sizeof (struct pollfd));
    pfd.fd = fd;
    pfd.events = POLLIN;

    LOG ("INO: Polling with " << VAR (timeout));
    poll (&pfd, 1, timeout * 1000);
    LOG ("INO: Poll returned.");
    
    if (pfd.revents & POLLIN) {
        char buffer[IE_BUFSIZE];
        read (fd, buffer, IE_BUFSIZE);

        struct inotify_event *ie = (struct inotify_event *) buffer;
        if (ie->len) {
            ev.filename = ie->name;
        }
        ev.flags = ie->mask;
        ev.watch = ie->wd;
        ev.cookie = ie->cookie;
        LOG ("INO: Got next event! " << VAR (ev.filename) << VAR (ev.watch) << VAR (ev.flags));
        return true;
    }

    return false;
}

