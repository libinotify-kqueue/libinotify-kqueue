/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2015 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include "fail_test.hh"

#define INVALID_FILENO		10000
#define NONINOTIFY_FILENO	1 /* stdout */

static std::jmp_buf jbuf;
static void catch_segv (int signo)
{
    std::longjmp (jbuf, 1);
}

fail_test::fail_test (journal &j)
: test ("Failures", j)
{
}

void fail_test::setup ()
{
    cleanup ();
    system ("touch fail-working");
}

void fail_test::run (bool direct)
{
    consumer cons(direct);
    sig_t oldsegvhandler;
    int wid = 0;
    int error = 0;

    wid = inotify_add_watch (INVALID_FILENO, "fail-working", IN_ALL_EVENTS);
    should ("watch id is -1, errno set to EBADF when starting watching an "
            "invalid file descriptor", wid == -1 && errno == EBADF);

    cons.output.reset ();
    error = inotify_rm_watch (INVALID_FILENO, 0);
    should ("watch id is -1, errno set to EBADF when stopping watching an "
            "invalid file descriptor", error == -1 && errno == EBADF);

    cons.output.reset ();
    wid = inotify_add_watch (NONINOTIFY_FILENO, "fail-working", IN_ALL_EVENTS);
    should ("watch id is -1, errno set to EINVAL when starting watching a "
            "valid noninotify file descriptor", wid == -1 && errno == EINVAL);

    cons.output.reset ();
    error = inotify_rm_watch (NONINOTIFY_FILENO, 0);
    should ("inotify_rm_watch returns -1, errno set to EINVAL when stopping "
            "watching a valid noninotify file descriptor",
            error == -1 && errno == EINVAL);

    cons.output.reset ();
    error = inotify_rm_watch (cons.get_fd (), INVALID_FILENO);
    should ("inotify_rm_watch returns -1, errno set to EINVAL when stopping "
            "watching an invalid watch descriptor",
            error == -1 && errno == EINVAL);

    cons.output.reset ();
    error = inotify_rm_watch (cons.get_fd (), NONINOTIFY_FILENO);
    should ("inotify_rm_watch returns -1, errno set to EINVAL when stopping "
            "watching an noninotify watch descriptor",
            error == -1 && errno == EINVAL);

    /* Add a watch, should fail */
    cons.output.reset ();
    cons.input.setup ("non-existent", IN_ALL_EVENTS);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    error = cons.output.added_watch_error ();
    should ("watch id is -1, errno set to ENOENT when starting watching a "
            "non-existent file", wid == -1 && error == ENOENT);

    cons.output.reset ();
    cons.input.setup ("fail-working", IN_ATTRIB | IN_ONLYDIR);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    error = cons.output.added_watch_error ();
    should ("watch id is -1, errno set to ENOTDIR when starting watching a "
            "file with IN_ONLYDIR flag set", wid == -1 && error == ENOTDIR);

    cons.output.reset ();
    cons.input.setup ("fail-working", 0);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    error = cons.output.added_watch_error ();
    should ("watch id is -1, errno set to EINVAL when starting watching a "
            "file with no event flags set", wid == -1 && error == EINVAL);

    if (geteuid() > 0) {
        cons.output.reset ();
        chmod ("fail-working", 0);
        cons.input.setup ("fail-working", IN_ALL_EVENTS);
        cons.output.wait ();

        wid = cons.output.added_watch_id ();
        error = cons.output.added_watch_error ();
        should ("watch id is -1, errno set to EACCES when starting watching a "
                "file without read access", wid == -1 && error == EACCES);
    } else {
        skip ("watch id is -1, errno set to EACCES when starting watching a "
              "file without read access (test is run with effective uid = 0)");
    }

    cons.output.reset ();
    oldsegvhandler = std::signal (SIGSEGV, catch_segv);
    if (setjmp(jbuf) == 0) {
        wid = inotify_add_watch (cons.get_fd (), (char *)-1, IN_ALL_EVENTS);
    } else {
        std::cout << "SIGSEGV catched!!! other tests will probably fail\n";
        wid = -1;
        errno = -SIGSEGV; /* Invalid errno */
    }
    std::signal (SIGSEGV, oldsegvhandler);
    should ("watch id is -1, errno set to EFAULT when starting watching a "
            "file with pathname pointing outside of the process's accessible "
            "address space", wid == -1 && errno == EFAULT);

    cons.input.interrupt ();
}

void fail_test::cleanup ()
{
    system ("rm -rf fail-working");
}
