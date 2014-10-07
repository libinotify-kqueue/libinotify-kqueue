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

#include <cstdlib>
#include "start_stop_test.hh"

/* Always present on Linux (not sure about a concrete 2.6.x release)
 * May be "to be implemented" on BSDs */
#ifndef IN_IGNORED
#  define IN_IGNORED	 0x00008000
#endif

start_stop_test::start_stop_test (journal &j)
: test ("Start-stop test", j)
{
}

void start_stop_test::setup ()
{
    cleanup ();
    system ("touch sst-working");
    system ("ln sst-working sst-working2");
    system ("ln -s sst-working sst-working3");
}

void start_stop_test::run ()
{
    consumer cons;
    int wid = 0;
    int wid2 = 0;
    events received;

    /* Add a watch */
    cons.input.setup ("sst-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch sst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("all produced events are registered",
            contains (received, event ("", wid, IN_ATTRIB)));

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
    
    /* Tell again to consumer to watch for an IN_ATTRIB event  */
    cons.output.reset ();
    cons.input.receive ();
    
    system ("touch sst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("events should not be registered on a removed watch",
            received.size() == 0);

    /* Now start watching again. Everything should work */
    cons.output.reset ();
    cons.input.setup ("sst-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("start watching a file after stop", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    cons.output.reset ();
    cons.input.receive ();

    /* Produce activity, consumer should watch it */
    system ("touch sst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("all produced events are registered after resume",
            contains (received, event ("", wid, IN_ATTRIB)));

    cons.output.reset ();
    cons.input.setup ("sst-working2", IN_ATTRIB);
    cons.output.wait ();

    wid2 = cons.output.added_watch_id ();
    should ("pair of hardlinked files should be opened with the same watch ID", wid == wid2);

    cons.output.reset ();
    cons.input.setup ("sst-working3", IN_ATTRIB);
    cons.output.wait ();

    wid2 = cons.output.added_watch_id ();
    if (should ("watch on file is added successfully via softlink", wid2 != -1)) {
        should ("pair of softlinked files should be opened with the same watch ID", wid == wid2);
    }

    cons.input.interrupt ();
}

void start_stop_test::cleanup ()
{
    system ("rm -rf sst-working");
    system ("rm -rf sst-working2");
    system ("rm -rf sst-working3");
}
