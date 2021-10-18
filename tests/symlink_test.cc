/*******************************************************************************
  Copyright (c) 2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <unistd.h>

#include <algorithm>
#include "symlink_test.hh"

symlink_test::symlink_test (journal &j)
: test ("Symbolic links", j)
{
}

void symlink_test::setup ()
{
    cleanup ();
    system ("mkdir slt-wd1");
    system ("touch slt-wd1/foo");
    system ("mkdir slt-wd2");
    system ("mkdir slt-wd3");
    system ("ln -s ../slt-wd1/foo slt-wd3/bar");
    system ("ln -s ../slt-wd1/foo slt-wd3/baz");
}

void symlink_test::run ()
{
    /* Issue #10 - do not reflect changes in files under watched symlinks */
    {   consumer cons;
        events received;
        events::iterator iter;
        int wid = 0;

        cons.input.setup ("slt-wd2",
                          IN_ATTRIB | IN_MODIFY
                          | IN_CREATE | IN_DELETE
                          | IN_MOVED_FROM | IN_MOVED_TO
                          | IN_MOVE_SELF | IN_DELETE_SELF);
        cons.output.wait ();
        wid = cons.output.added_watch_id ();

        cons.output.reset ();
        cons.input.receive ();

        system ("ln -s ../slt-wd1/foo slt-wd2/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("receive IN_CREATE for slt-wd2/bar",
                contains (received, event ("bar", wid, IN_CREATE)));


        cons.output.reset ();
        cons.input.receive ();

        system ("touch slt-wd2/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("No IN_ATTRIB after touching symlink", received.empty());


        cons.output.reset ();
        cons.input.receive ();

        system ("touch slt-wd1/foo");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("No IN_ATTRIB after touching symlink source file", received.empty());


        cons.output.reset ();
        cons.input.receive ();

        system ("echo hello >> slt-wd2/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("No IN_MODIFY after modifying a file via symlink", received.empty());


        cons.output.reset ();
        cons.input.receive ();

        system ("echo hello >> slt-wd1/foo");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("No IN_MODIFY after modifying symlink source file", received.empty());


        cons.output.reset ();
        cons.input.receive ();

        system ("rm slt-wd2/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("Receive IN_DELETE on removing a symlink from the watched directory",
                contains (received, event ("bar", wid, IN_DELETE)));

        cons.input.interrupt ();
    }
    /* For the direct user watches (not dependencies), symlinks should work as usual */
    {   consumer cons;
        events received;
        events::iterator iter;
        int wid = 0;

        cons.input.setup ("slt-wd3/bar",
                          IN_ATTRIB | IN_MODIFY
                          | IN_CREATE | IN_DELETE
                          | IN_MOVED_FROM | IN_MOVED_TO
                          | IN_MOVE_SELF | IN_DELETE_SELF);
        cons.output.wait ();
        wid = cons.output.added_watch_id ();

        cons.output.reset ();
        cons.input.receive ();

        system ("touch slt-wd3/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("Receive IN_ATTRIB after touching symlink",
                contains (received, event ("", wid, IN_ATTRIB)));


        cons.output.reset ();
        cons.input.receive ();

        system ("touch slt-wd1/foo");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("Receive IN_ATTRIB after touching symlink source file",
                contains (received, event ("", wid, IN_ATTRIB)));


        cons.output.reset ();
        cons.input.receive ();

        system ("echo hello >> slt-wd3/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("Receive IN_MODIFY after modifying a file via symlink",
                contains (received, event ("", wid, IN_MODIFY)));


        cons.output.reset ();
        cons.input.receive ();

        system ("echo hello >> slt-wd1/foo");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("Receive IN_MODIFY after modifying symlink source file",
                contains (received, event ("", wid, IN_MODIFY)));


        /* For me, this behavior is a bit weird, but it is still what we see in Linux: */
        cons.output.reset ();
        cons.input.receive ();

        system ("rm slt-wd3/bar");

        cons.output.wait ();
        received = cons.output.registered ();
        should ("No IN_DELETE_SELF on removing a symlink", received.empty());

        cons.input.interrupt ();
    }
    /* Test IN_DONT_FOLLOW */
    {   consumer cons;
        events received;
        events::iterator iter;
        int wid = 0;

        cons.input.setup ("slt-wd3/baz",
                          IN_ATTRIB | IN_MODIFY
                          | IN_CREATE | IN_DELETE
                          | IN_MOVED_FROM | IN_MOVED_TO
                          | IN_MOVE_SELF | IN_DELETE_SELF
                          | IN_DONT_FOLLOW);
        cons.output.wait ();
        wid = cons.output.added_watch_id ();
#if defined(__linux__) || defined(O_SYMLINK)
        should ("Start watch successfully on a symlink file with IN_DONT_FOLLOW",
                wid != -1);
#else
        skip ("Start watch successfully on a symlink file with IN_DONT_FOLLOW"
              " (O_SYMLINK open() flag missed)");
#endif

        cons.output.reset ();
        cons.input.receive ();

        lchown ("slt-wd3/baz", getuid(), getgid());

        cons.output.wait ();
        received = cons.output.registered ();
#if defined(__linux__) || defined(O_SYMLINK)
        should ("Receive IN_ATTRIB after touching symlink itself",
                contains (received, event ("", wid, IN_ATTRIB)));
#else
        skip ("Receive IN_ATTRIB after touching symlink itself"
              " (O_SYMLINK open() flag missed)");
#endif


        cons.output.reset ();
        cons.input.receive ();

        system ("echo hello >> slt-wd1/foo");

        cons.output.wait ();
        received = cons.output.registered ();
#if defined(__linux__) || defined(O_SYMLINK)
        should ("No IN_MODIFY after modifying symlink source file",
                !contains (received, event ("", wid, IN_MODIFY)));
#else
        skip ("No IN_MODIFY after modifying symlink source file"
              " (O_SYMLINK open() flag missed)");
#endif


        cons.output.reset ();
        cons.input.receive ();

        system ("echo hello >> slt-wd3/baz");

        cons.output.wait ();
        received = cons.output.registered ();
#if defined(__linux__) || defined(O_SYMLINK)
        should ("No IN_MODIFY after modifying file via symlink",
                !contains (received, event ("", wid, IN_MODIFY)));
#else
        skip ("No IN_MODIFY after modifying file via symlink"
              " (O_SYMLINK open() flag missed)");
#endif

        cons.output.reset ();
        cons.input.receive ();

        system ("mv slt-wd3/baz slt-wd3/bazz");

        cons.output.wait ();
        received = cons.output.registered ();
#if defined(__linux__) || defined(O_SYMLINK)
        should ("Receive IN_MOVE_SELF after moving the symlink",
                contains (received, event ("", wid, IN_MOVE_SELF)));
#else
        skip ("Receive IN_MOVE_SELF after moving the symlink"
              " (O_SYMLINK open() flag missed)");
#endif


        cons.output.reset ();
        cons.input.receive ();

        system ("rm slt-wd3/bazz");

        cons.output.wait ();
        received = cons.output.registered ();
#if defined(__linux__) || defined(O_SYMLINK)
        should ("Receive IN_DELETE_SELF after removing the symlink",
                contains (received, event ("", wid, IN_DELETE_SELF)));
#else
        skip ("Receive IN_DELETE_SELF after removing the symlink"
              " (O_SYMLINK open() flag missed)");
#endif

        cons.input.interrupt ();
    }
}

void symlink_test::cleanup ()
{
    system ("rm -rf slt-wd1");
    system ("rm -rf slt-wd2");
    system ("rm -rf slt-wd3");
}
