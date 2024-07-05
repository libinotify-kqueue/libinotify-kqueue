/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2024 Serenity Cybersecurity, LLC
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
#include <cerrno>
#include <algorithm>
#include "log.hh"
#include "consumer.hh"

consumer::consumer (bool direct)
: ino(direct)
{
    pthread_create (&self, NULL, consumer::run_, this);
}

consumer::~consumer ()
{
    // It is a trick. The consumer object lives in a separate thread that is created
    // in its constructor. However, the object itself is created in another (parent)
    // thread, so the destructor should work in the same thread (counting on static
    // allocation).
    LOG ("CONS: Joining on self");
    pthread_join (self, NULL);
}

void* consumer::run_ (void *ptr)
{
    assert (ptr != NULL);
    ((consumer *) ptr)->run();
    return NULL;
}

void consumer::register_activity (request::activity activity)
{
    events received = ino.receive_during (activity.timeout);
    LOG ("CONS: Okay, informing producer about results...");
    input.reset ();
    output.setup (received);
}

void consumer::add_modify_watch (request::add_modify add_modify)
{
    uint32_t id = ino.watch (add_modify.path, add_modify.mask);
    int error = errno;
    LOG ("CONS: Added watch");
    input.reset ();
    output.setup (id, error);
}

void consumer::remove_watch (request::remove remove)
{
    int retval = ino.cancel (remove.watch_id);
    int error = errno;
    LOG ("CONS: Cancelled watch");
    input.reset ();
    output.setup (retval, error);
}

void consumer::run ()
{
    while (input.wait ()) {
        switch (input.current_variant ()) {
        case request::REGISTER_ACTIVITY:
            register_activity (input.activity_data ());
            break;

        case request::ADD_MODIFY_WATCH:
            add_modify_watch (input.add_modify_data ());
            break;

        case request::REMOVE_WATCH:
            remove_watch (input.remove_data ());
            break;
        }

        LOG ("CONS: Sleeping on input");
    }
}

int consumer::get_fd ()
{
    return ino.get_fd ();
}
