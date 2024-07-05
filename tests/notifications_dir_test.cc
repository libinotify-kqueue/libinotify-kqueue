/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
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

#ifndef __linux__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#include <algorithm>
#include "config.h"
#include "notifications_dir_test.hh"

notifications_dir_test::notifications_dir_test (journal &j)
: test ("Directory notifications", j)
{
}

void notifications_dir_test::setup ()
{
    cleanup ();

    system ("mkdir ntfsdt-working");
    system ("touch ntfsdt-working/foo");
    system ("touch ntfsdt-working/bar");

    system ("mkdir ntfsdt-cache");
    system ("touch ntfsdt-cache/bar");

    system ("mkdir ntfsdt-bugs");
    system ("touch ntfsdt-bugs/1");
    system ("touch ntfsdt-bugs/2");
}

void notifications_dir_test::run (bool direct)
{
    consumer cons(direct);
    events received;
    events::iterator iter;
    int wid = 0;

    /* Add a watch */
    cons.input.setup ("ntfsdt-working",
                      IN_ATTRIB | IN_MODIFY | IN_ACCESS
                      | IN_CREATE | IN_DELETE
                      | IN_MOVED_FROM | IN_MOVED_TO
                      | IN_MOVE_SELF | IN_DELETE_SELF);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ntfsdt-working");

    cons.output.wait ();
    received = cons.output.registered ();

    iter = std::find_if (received.begin(),
                         received.end(),
                         event_matcher (event ("", wid, IN_ATTRIB | IN_ISDIR)));
    should ("receive IN_ATTRIB event on touch on a directory",
            iter != received.end() && iter->flags & IN_ATTRIB);
    should ("the touch event for a directory contains IN_ISDIR in the flags",
            iter != received.end() && iter->flags & IN_ISDIR);


    cons.output.reset ();
    cons.input.receive ();

    system ("ls ntfsdt-working >> /dev/null");

    cons.output.wait ();
    received = cons.output.registered ();
#if defined(__linux__) || defined(NOTE_READ)
    should ("receive IN_ACCESS event on reading of directory contents",
            contains (received, event ("", wid, IN_ACCESS)));
#else
    skip ("receive IN_ACCESS event on reading of directory contents"
          " (NOTE_READ kqueue event missed)");
#endif


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ntfsdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_CREATE event for a new entry in a directory (created with touch)",
            contains (received, event ("1", wid, IN_CREATE)));


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ntfsdt-working/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_CREATE event for a new entry in a directory (created with echo)",
            contains (received, event ("2", wid, IN_CREATE)));


    cons.output.reset ();
    cons.input.receive ();

    system ("rm ntfsdt-working/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_DELETE event on deleting a file from a directory",
            contains (received, event ("2", wid, IN_DELETE)));


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-working/1 ntfsdt-working/one");

    cons.output.wait ();
    received = cons.output.registered ();

    events::iterator iter_from, iter_to;
    iter_from = std::find_if (received.begin(),
                              received.end(),
                              event_matcher (event ("1", wid, IN_MOVED_FROM)));
    iter_to = std::find_if (received.begin(),
                            received.end(),
                            event_matcher (event ("one", wid, IN_MOVED_TO)));

    if (should ("receive IN_MOVED_FROM and IN_MOVED_TO for rename",
                iter_from != received.end () && iter_to != received.end())) {
        should ("both events for a rename have the same cookie",
                iter_from->cookie == iter_to->cookie);
    }


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ntfsdt-working/one");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_MODIFY event on modifying an entry in a directory",
            contains (received, event ("one", wid, IN_MODIFY)));


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-working/one ntfsdt-cache/one");

    cons.output.wait ();
    received = cons.output.registered ();
#if defined(__linux__) || defined(HAVE_NOTE_EXTEND_ON_MOVE_FROM)
    should ("receive IN_MOVED_FROM event on moving file from directory "
            "to another location within the same mount point",
            contains (received, event ("one", wid, IN_MOVED_FROM)));
#else
    skip ("receive IN_MOVED_FROM event on moving file from directory "
          "to another location within the same mount point "
          "(parent NOTE_EXTEND kqueue event missed on rename)");
#endif


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-cache/one ntfsdt-working/one");

    cons.output.wait ();
    received = cons.output.registered ();
#if defined(__linux__) || defined(HAVE_NOTE_EXTEND_ON_MOVE_TO)
    should ("receive IN_MOVED_TO event on moving file to directory "
            "from another location within the same mount point",
            contains (received, event ("one", wid, IN_MOVED_TO)));
#else
    skip ("receive IN_MOVED_TO event on moving file to directory "
          "from another location within the same mount point "
          "(parent NOTE_EXTEND kqueue event missed on rename)");
#endif


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-working/foo ntfsdt-working/bar");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive all move events when replaced a file in a directory "
            "with another file from the same directory",
            contains (received, event ("foo", wid, IN_MOVED_FROM))
            && contains (received, event ("bar", wid, IN_MOVED_TO)));


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ntfsdt-working/bar");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive events from a file, which has replaced a file in a directory",
            contains (received, event ("bar", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-cache/bar ntfsdt-working/bar");

    /* Interesting test case here.
     * Looks like inotify sends IN_MOVED_TO when overwriting a file with a file from
     * the same partition/fs, and sends a pair of IN_DELETE/IN_CREATE when overwriting
     * a file from a different partition/fs.
     */
    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive events when overwriting a file in a directory"
            " with an external file",
            (contains (received, event ("bar", wid, IN_DELETE))
             && contains (received, event ("bar", wid, IN_CREATE)))
            || (contains (received, event ("bar", wid, IN_MOVED_TO))));


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ntfsdt-working/bar");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive events from a file, which has overwritten a file in a directory",
            contains (received, event ("bar", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.receive ();

    system ("mkdir ntfsdt-working/dir");

    cons.output.wait ();
    received = cons.output.registered();

    iter = std::find_if (received.begin(),
                         received.end(),
                         event_matcher (event ("dir", wid, IN_CREATE)));
    should ("receive IN_CREATE with IN_ISDIR when creating a directory in directory",
            iter != received.end() && iter->flags & IN_ISDIR);


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ntfsdt-working/dir");

    cons.output.wait ();
    received = cons.output.registered();

    iter = std::find_if (received.begin(),
                         received.end(),
                         event_matcher (event ("dir", wid, IN_ATTRIB)));
    should ("receive IN_ATTRIB with IN_ISDIR when touching a directory in directory",
            iter != received.end() && iter->flags & IN_ISDIR);


    cons.output.reset ();
    cons.input.receive ();

    system ("ls ntfsdt-working/dir >> /dev/null");

    cons.output.wait ();
    received = cons.output.registered ();

    iter = std::find_if (received.begin(),
                         received.end(),
                         event_matcher (event ("dir", wid, IN_ACCESS)));
#if defined(__linux__) || defined(NOTE_READ)
    should ("receive IN_ACCESS with IN_ISDIR on reading of subdirectory contents",
            iter != received.end() && iter->flags & IN_ISDIR);
#else
    skip ("receive IN_ACCESS with IN_ISDIR on reading of subdirectory contents"
          " (NOTE_READ kqueue event missed)");
#endif


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-working/dir ntfsdt-working/dirr");

    cons.output.wait ();
    received = cons.output.registered();

    iter_from = std::find_if (received.begin(),
                              received.end(),
                              event_matcher (event ("dir", wid, IN_MOVED_FROM)));
    iter_to = std::find_if (received.begin(),
                            received.end(),
                            event_matcher (event ("dirr", wid, IN_MOVED_TO)));

    if (should ("receive IN_MOVED_FROM and IN_MOVED_TO for directory rename in directory ",
                iter_from != received.end () && iter_to != received.end())) {
        should ("both events for a dir rename have the same cookie",
                iter_from->cookie == iter_to->cookie);
        should ("both events for a dir rename have IN_ISDIR",
                (iter_from->flags & IN_ISDIR) && (iter_to->flags & IN_ISDIR));
    }


    cons.output.reset ();
    cons.input.receive ();

    system ("rm -rf ntfsdt-working/dirr");

    cons.output.wait ();
    received = cons.output.registered();

    iter = std::find_if (received.begin(),
                         received.end(),
                         event_matcher (event ("dirr", wid, IN_DELETE)));

    should ("receive IN_DELETE with IN_ISDIR when removing directory in directory",
            iter != received.end () &&  (iter->flags & IN_ISDIR));


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-working ntfsdt-working-2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive a move event",
            contains (received, event ("", wid, IN_MOVE_SELF)));


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfsdt-working-2/bar ntfsdt-working-2/foo");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive events from a files in directory after directory has been moved",
            contains (received, event ("bar", wid, IN_MOVED_FROM))
            && contains (received, event ("foo", wid, IN_MOVED_TO)));

    /* resetup watch again to not be dependent on success of previous test */
    cons.input.setup ("ntfsdt-working-2",
                      IN_ATTRIB | IN_MODIFY
                      | IN_CREATE | IN_DELETE
                      | IN_MOVED_FROM | IN_MOVED_TO
                      | IN_MOVE_SELF | IN_DELETE_SELF);
    cons.output.wait ();
    wid = cons.output.added_watch_id ();

    cons.output.reset ();
    cons.input.receive ();

    system ("rm -rf ntfsdt-working-2");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive IN_DELETE for a file \'one\' in a directory on removing a directory",
            contains (received, event ("one", wid, IN_DELETE)));
    should ("receive IN_DELETE for a file \'foo\' in a directory on removing a directory",
            contains (received, event ("foo", wid, IN_DELETE)));
    should ("receive IN_DELETE_SELF on removing a directory",
            contains (received, event ("", wid, IN_DELETE_SELF)));
    should ("receive IN_IGNORED on removing a directory",
            contains (received, event ("", wid, IN_IGNORED)));


    cons.input.interrupt ();
}

void notifications_dir_test::cleanup ()
{
    system ("rm -rf ntfsdt-working-2");
    system ("rm -rf ntfsdt-working");
    system ("rm -rf ntfsdt-cache");
    system ("rm -rf ntfsdt-bugs");
}
