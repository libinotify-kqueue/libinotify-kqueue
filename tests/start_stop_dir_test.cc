/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014 Vladimir Kondratiev <wulf@cicgroup.ru>

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
#include "start_stop_dir_test.hh"

/* Always present on Linux (not sure about a concrete 2.6.x release)
 * May be "to be implemented" on BSDs */
#ifndef IN_IGNORED
#  define IN_IGNORED	 0x00008000
#endif

start_stop_dir_test::start_stop_dir_test (journal &j)
: test ("Start-stop directory", j)
{
}

void start_stop_dir_test::setup ()
{
    cleanup ();
    
    system ("mkdir ssdt-working");
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");
}

void start_stop_dir_test::run ()
{
    consumer cons;
    events received;
    int wid = 0;

    /* Add a watch */
    cons.input.setup ("ssdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    cons.output.reset ();
    cons.input.receive ();

    /* Produce activity */
    system ("touch ssdt-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("events are registered on a directory",
            contains (received, event ("", wid, IN_ATTRIB)));

    /* Watch should also signal us about activity on files at the watched directory. */
    cons.output.reset ();
    cons.input.receive ();

    /* This events should be registered */
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");
    
    cons.output.wait ();
    received = cons.output.registered ();
    should ("events are registered on the directory contents",
            contains (received, event ("1", wid, IN_ATTRIB))
            && contains (received, event ("2", wid, IN_ATTRIB))
            && contains (received, event ("3", wid, IN_ATTRIB)));

    /* Now stop watching */
    cons.output.reset ();
    cons.input.setup (wid);
    cons.output.wait ();

    /* Linux inotify sends IN_IGNORED on stop */
    cons.output.reset ();
    cons.input.receive ();
    
    cons.output.wait ();
    received = cons.output.registered ();
    should ("got IN_IGNORED on watch stop",
            contains (received, event ("", wid, IN_IGNORED)));

    /* These events should not be visible to watch */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working");
    system ("touch ssdt-working/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("items on a stopped watch are unregistered", received.empty ());

    /* Now resume watching */
    cons.output.reset ();
    cons.input.setup ("ssdt-working", IN_ATTRIB);

    cons.output.wait ();
    wid = cons.output.added_watch_id ();
    should ("watch is added successfully again", wid != -1);

    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working");
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive all events on a resumed watch",
            contains (received, (event ("", wid, IN_ATTRIB)))
            && contains (received, (event ("1", wid, IN_ATTRIB)))
            && contains (received, (event ("2", wid, IN_ATTRIB)))
            && contains (received, (event ("3", wid, IN_ATTRIB))));

    /* Now, start watching on a file from a directory manually */
    int child_wid = 0;

    cons.output.reset ();
    cons.input.setup ("ssdt-working/3", IN_ATTRIB);

    cons.output.wait ();
    child_wid = cons.output.added_watch_id ();
    should ("watch on a file in a directory is added successfully", wid != -1);

    /* On a single touch, should recive two events */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working/3");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive events for a same file from both watches "
            "(sometimes this test fails event on Linux, at least on 2.6.39)",
            contains (received, event ("3", wid, IN_ATTRIB))
            && contains (received, event ("", child_wid, IN_ATTRIB)));

    /* Now stop a directory watch */
    cons.output.reset ();
    cons.input.setup (wid);
    cons.output.wait ();

    /* Linux inotify sends IN_IGNORED on stop */
    cons.output.reset ();
    cons.input.receive ();
    cons.output.wait ();

    /* Still should be able to receive notifications from an additional watch */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working/3");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("after stop on a directory watch, "
            "receive only a single event from a file watch",
            contains (received, event ("", child_wid, IN_ATTRIB)));

    /* IN_ONESHOT flag tests */
    cons.input.setup ("ssdt-working", IN_CREATE | IN_DELETE | IN_ONESHOT);
    cons.output.wait ();
    wid = cons.output.added_watch_id ();

    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working/4");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_CREATE for ssdt_working on touch",
            contains (received, event ("4", wid, IN_CREATE)));
    should ("receive IN_IGNORED after one event have been received "
            "if watch was opened with IN_ONESHOT flag set",
            contains (received, event ("", wid, IN_IGNORED)));

    cons.output.reset ();
    cons.input.receive ();

    system ("rm ssdt-working/4");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("Stop receiving events after one event have been received "
            "if watch was opened with IN_ONESHOT flag set",
            !contains (received, event ("4", wid, IN_DELETE)));

    cons.input.interrupt ();
}

void start_stop_dir_test::cleanup ()
{
    system ("rm -rf ssdt-working");
}
