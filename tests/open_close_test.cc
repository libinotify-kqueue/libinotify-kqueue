/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
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
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>

#include "open_close_test.hh"

open_close_test::open_close_test (journal &j)
: test ("Open/close notifications", j)
{
}

void open_close_test::setup ()
{
    cleanup ();
    system ("touch oct-file-working");
    system ("echo Hello >> oct-file-working");
    system ("mkdir oct-dir-working");
}

void open_close_test::run ()
{
    consumer cons;
    int file_wid = 0;
    int dir_wid = 0;
    events received;

    cons.input.setup ("oct-file-working", IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
    cons.output.wait ();

    file_wid = cons.output.added_watch_id ();
    should ("start watching on a file", file_wid != -1);


#if defined(__linux__) || defined(NOTE_OPEN)
    cons.output.reset ();
    cons.input.setup ("oct-dir-working", IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
    cons.output.wait ();

    dir_wid = cons.output.added_watch_id ();
    should ("start watching on a directory", dir_wid != -1);

    cons.output.reset ();
    cons.input.receive ();

    system ("cat oct-file-working >> /dev/null");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_OPEN on cat",
            contains (received, event ("", file_wid, IN_OPEN)));
    should ("receive IN_CLOSE_NOWRITE on cat",
            contains (received, event ("", file_wid, IN_CLOSE_NOWRITE)));
#else
    skip ("receive IN_OPEN on cat (NOTE_OPEN kqueue event missed)");
    skip ("receive IN_CLOSE_NOWRITE on cat (NOTE_CLOSE kqueue event missed)");
#endif


#if defined(__linux__) || defined(NOTE_OPEN)
    cons.output.reset ();
    cons.input.receive ();

    system ("ls oct-dir-working >> /dev/null");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_OPEN on ls",
            contains (received, event ("", dir_wid, IN_OPEN)));
    should ("receive IN_CLOSE_NOWRITE on ls",
            contains (received, event ("", dir_wid, IN_CLOSE_NOWRITE)));
#else
    skip ("receive IN_OPEN on ls (NOTE_OPEN kqueue event missed)");
    skip ("receive IN_CLOSE_NOWRITE on ls (NOTE_CLOSE kqueue event missed)");
#endif


#if defined(__linux__) || defined(NOTE_OPEN)
    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> oct-file-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_OPEN on modify",
            contains (received, event ("", file_wid, IN_OPEN)));
    should ("receive IN_CLOSE_WRITE on modify",
            contains (received, event ("", file_wid, IN_CLOSE_WRITE)));
#else
    skip ("receive IN_OPEN on modify (NOTE_OPEN kqueue event missed)");
    skip ("receive IN_CLOSE_WRITE on modify (NOTE_CLOSE_WRITE kqueue event missed)");
#endif

    cons.input.interrupt ();
}

void open_close_test::cleanup ()
{
    system ("rm -rf oct-file-working");
    system ("rm -rf oct-dir-working");
}
