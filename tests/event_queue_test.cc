/*******************************************************************************
  Copyright (c) 2016 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <cstdlib>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <iterator>

#include "event_queue_test.hh"

#ifdef __linux__
#define QUEUED_EVENTS  16386 /* default /proc/sys/fs/inotify/max_queued_events */
#define PIPED_EVENTS   0     /* Direct reads from in-kernel queue */
#define EVENT_INTERVAL 0
#else
#define QUEUED_EVENTS  64
#define PIPED_EVENTS   64
#define EVENT_INTERVAL 2000   /* max time to process kqueue event by worker, us */
#endif

event_queue_test::event_queue_test (journal &j)
: test ("Inotify event queue", j)
{
}

void event_queue_test::setup ()
{
    cleanup ();
    system ("mkdir eqt-working");
    system ("touch eqt-working/1");
}

void event_queue_test::run (bool direct)
{
    consumer cons(direct);
    events received;
    int wid = 0;


    cons.input.setup ("eqt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);


    cons.output.reset ();

    system ("touch eqt-working");
    usleep (EVENT_INTERVAL);
    system ("touch eqt-working");
    usleep (EVENT_INTERVAL);

    cons.input.receive ();
    cons.output.wait ();
    received = cons.output.registered ();
    if (!direct)
        should ("receive single (coalesced) IN_ATTRIB on 2 consecutive "
                "watched dir touches", received.size() == 1 &&
                contains (received, event ("", wid, IN_ATTRIB)));


    cons.output.reset ();

    system ("touch eqt-working/1");
    usleep (EVENT_INTERVAL);
    system ("touch eqt-working/1");
    usleep (EVENT_INTERVAL);

    cons.input.receive ();
    cons.output.wait ();
    received = cons.output.registered ();
    if (!direct)
        should ("receive single (coalesced) IN_ATTRIB on 2 consecutive "
                "subfile touches", received.size() == 1 &&
                contains (received, event ("1", wid, IN_ATTRIB)));


    /*
     * Reduce pipe and event queue lengths to speed up overflow testing
     * Note: average event size is (sizeof (struct inotify_event) + 1) bytes
     *       as we generate parent event of sizeof (struct inotify_event) bytes
     *       and subfile event of (sizeof (struct inotify_event) + 2) bytes
     */
#ifdef __linux__
//    system ("echo "QUEUED_EVENTS" > /proc/sys/fs/inotify/max_queued_events");
#else
    libinotify_set_param (cons.get_fd (), IN_SOCKBUFSIZE,
                          PIPED_EVENTS * (sizeof (struct inotify_event) + 1));
    libinotify_set_param (cons.get_fd (), IN_MAX_QUEUED_EVENTS, QUEUED_EVENTS);
#endif
    cons.output.reset ();

    for (int i = 0; i < (QUEUED_EVENTS + PIPED_EVENTS) / 2 + 1; i++) {
        /* Generate 2 different events to prevent coalescing */
        /* parent directory event size is sizeof (struct inotify_event) */
        system ("touch eqt-working");
        usleep (EVENT_INTERVAL);
        /* subfile event size is sizeof(struct inotify_event)+strlen("1")+1 */
        system ("touch eqt-working/1");
        usleep (EVENT_INTERVAL);
    }

    cons.input.receive ();
    cons.output.wait ();
    received = cons.output.registered ();
    if (!direct)
        should ("receive IN_Q_OVERFLOW on many consecutive touches",
                contains (received, event ("", -1, IN_Q_OVERFLOW)));


    cons.input.interrupt ();
}

void event_queue_test::cleanup ()
{
    system ("rm -rf eqt-working");
}
