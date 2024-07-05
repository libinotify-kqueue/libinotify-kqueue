/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2024 Serenity Cyber Security, LLC
                     Author: Gleb Popov <arrowd@FreeBSD.org>
  SPDX-License-Identifier: MIT

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

#include <cassert>
#include <cstring>
#include <poll.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <unistd.h>
#include "inotify_client.hh"
#include "log.hh"

#include <iostream>

inotify_client::inotify_client (bool direct)
: fd (inotify_init1(direct ? O_DIRECT : 0))
, direct(direct)
{
    assert (fd != -1);
}

inotify_client::~inotify_client ()
{
    if (direct)
        libinotify_direct_close(fd);
    else
        close (fd);
}

int inotify_client::watch (const std::string &filename, uint32_t flags)
{
    assert (fd != -1);
    LOG ("INO: Adding " << VAR (filename) << VAR (flags));

    int retval = inotify_add_watch (fd, filename.c_str(), flags);
    LOG ("INO: " << VAR (retval));
    return retval;
}

int inotify_client::cancel (int watch_id)
{
    assert (fd != -1);
    int retval = inotify_rm_watch (fd, watch_id);
    if (retval != 0) {
        LOG ("INO: rm watch failed " << VAR (fd) << VAR (watch_id));
    }
    return retval;
}

#ifdef IN_DEF_SOCKBUFSIZE
#define IE_BUFSIZE IN_DEF_SOCKBUFSIZE
#else
#define IE_BUFSIZE (((sizeof (struct inotify_event) + FILENAME_MAX)) * 20)
#endif

void inotify_client::read_events (int fd, events &evs)
{
    char buffer[IE_BUFSIZE];
    char *ptr = buffer;
    int avail = read (fd, buffer, IE_BUFSIZE);

    /* The construction is probably harmful */
    while (avail >= sizeof (struct inotify_event *)) {
        struct inotify_event *ie = (struct inotify_event *) ptr;
        event ev;

        if (ie->len) {
                ev.filename = ie->name;
        }
        ev.flags = ie->mask;
        ev.watch = ie->wd;
        ev.cookie = ie->cookie;

        LOG ("INO: Got next event! " << VAR (ev.filename) << VAR (ev.watch) << VAR (ev.flags));
        evs.insert (ev);

        int offset = sizeof (struct inotify_event) + ie->len;
        avail -= offset;
        ptr += offset;
    }
}

void inotify_client::read_events_direct (int fd, events &evs)
{
    struct iovec *received[5];
    int num_events = libinotify_direct_readv (fd, received, 5, 0);

    for (int i = 0; i < num_events; i++) {
        struct iovec *cur_event = received[i];

        while (cur_event->iov_base) {
            struct inotify_event *ie = (struct inotify_event *) cur_event->iov_base;
            event ev;

            if (ie->len) {
                    ev.filename = ie->name;
            }
            ev.flags = ie->mask;
            ev.watch = ie->wd;
            ev.cookie = ie->cookie;

            LOG ("INO: Got next event! " << VAR (ev.filename) << VAR (ev.watch) << VAR (ev.flags));
            evs.insert (ev);

            cur_event++;
        }

        libinotify_free_iovec (received[i]);
    }
}

events inotify_client::receive_during (int timeout) const
{
    events received;
    struct pollfd pfd;

    time_t start = timems ();
    time_t elapsed = 0;

    LOG ("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");

    while ((elapsed = timems () - start) < timeout) {
        memset (&pfd, 0, sizeof (struct pollfd));
        pfd.fd = fd;
        pfd.events = POLLIN;

        LOG ("INO: Polling with " << VAR (timeout) << " ms");
        int pollretval = poll (&pfd, 1, timeout);
        LOG ("INO: Poll returned " << VAR (pollretval) << ", " << VAR(pfd.revents));
        if (pollretval == -1) {
            return events();
        }

        if (pfd.revents & POLLIN) {
            if (direct) {
                read_events_direct (fd, received);
            } else {
                read_events (fd, received);
            }
        }
    }

    LOG ("vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv");

    return received;
}

long inotify_client::bytes_available (int fd)
{
    long int avail = 0;
    size_t bytes;

    if (ioctl (fd, FIONREAD, (char *) &bytes) >= 0)
        avail = (long int) *((int *) &bytes);

    return avail;
}

int inotify_client::get_fd ()
{
    return fd;
}

time_t inotify_client::timems ()
{
    struct timespec ts;

#ifdef CLOCK_MONOTONIC
    if (clock_gettime (CLOCK_MONOTONIC, &ts) == 0) {
        return ts.tv_sec * 1000 + (time_t)(ts.tv_nsec / 1000000L);
    }
#endif

    return time(NULL) * 1000;
}
