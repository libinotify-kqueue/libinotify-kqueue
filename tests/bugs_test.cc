/*******************************************************************************
  Copyright (c) 2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <algorithm>
#include <unistd.h>
#include "bugs_test.hh"

bugs_test::bugs_test (journal &j)
: test ("Bugfix tests", j)
{
}

void bugs_test::setup ()
{
    cleanup ();
    system ("mkdir bugst-workdir");
    system ("touch bugst-workdir/1");
    system ("touch bugst-workdir/2");

}

void bugs_test::run (bool direct)
{
    consumer cons(direct);
    events received;
    events::iterator iter;
    int wid = 0;

    /* Issue #12 - directory diff calculation is not triggered for now-empty directories. */
    cons.input.setup ("bugst-workdir",
                      IN_ATTRIB | IN_MODIFY
                      | IN_CREATE | IN_DELETE
                      | IN_MOVED_FROM | IN_MOVED_TO
                      | IN_MOVE_SELF | IN_DELETE_SELF);
    cons.output.wait ();
    wid = cons.output.added_watch_id ();

    cons.output.reset ();
    cons.input.receive ();

    system ("rm bugst-workdir/1");
    system ("rm bugst-workdir/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_DELETE for bugst-workdir/1",
            contains (received, event ("1", wid, IN_DELETE)));
    should ("receive IN_DELETE for bugst-workdir/2",
            contains (received, event ("2", wid, IN_DELETE)));


    /* Test for extraneous IN_ATTRIB event on subdirectory creation and deletion */
    cons.output.reset ();
    cons.input.receive ();

    system ("mkdir bugst-workdir/1");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_CREATE for bugst-workdir/1",
            contains (received, event ("1", wid, IN_CREATE)));
    should ("Not receive IN_ATTRIB for bugst-workdir on subdirectory creation",
            !contains (received, event ("", wid, IN_ATTRIB)));

    cons.output.reset ();
    cons.input.receive ();

    system ("rmdir bugst-workdir/1");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_DELETE for bugst-workdir/1",
            contains (received, event ("1", wid, IN_DELETE)));
    should ("Not receive IN_ATTRIB for bugst-workdir on subdirectory deletion",
            !contains (received, event ("", wid, IN_ATTRIB)));


    /* Test for not issuing IN_DELETE_SELF on hardlink deletes */
    system ("touch bugst-workdir/1");
    system ("ln bugst-workdir/1 bugst-workdir/2");

    cons.input.setup ("bugst-workdir/1", IN_ATTRIB | IN_DELETE_SELF);
    cons.output.wait ();
    wid = cons.output.added_watch_id ();

    cons.output.reset ();
    cons.input.receive ();

    system ("rm bugst-workdir/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_ATTRIB for bugst-workdir/1 on hardlink delete",
            contains (received, event ("", wid, IN_ATTRIB)));
    should ("Not receive IN_DELETE_SELF for bugst-workdir/1 on hardlink delete",
            !contains (received, event ("", wid, IN_DELETE_SELF)));


    /* Test subwatch adding when no IN_(CREATE|DELETE|MOVE) flags specified */
    cons.input.setup ("bugst-workdir", IN_ATTRIB | IN_MODIFY);
    cons.output.wait ();
    wid = cons.output.added_watch_id ();

    cons.output.reset ();
    cons.input.receive ();

    system ("touch bugst-workdir/2");
    usleep (20000); /* Gives some time for readdir() to complete */
    system ("touch bugst-workdir/2");
    system ("echo test >> bugst-workdir/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_ATTRIB for bugst-workdir/2 on touch",
            contains (received, event ("2", wid, IN_ATTRIB)));
    should ("receive IN_MODIFY for bugst-workdir/2 on echo",
            contains (received, event ("2", wid, IN_MODIFY)));

    cons.input.interrupt ();
}

void bugs_test::cleanup ()
{
    system ("rm -rf bugst-workdir");
}
