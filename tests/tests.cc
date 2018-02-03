/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
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

#include <stdlib.h>
#include "start_stop_test.hh"
#include "start_stop_dir_test.hh"
#include "notifications_test.hh"
#include "notifications_dir_test.hh"
#include "fail_test.hh"
#include "update_flags_test.hh"
#include "update_flags_dir_test.hh"
#include "open_close_test.hh"
#include "symlink_test.hh"
#include "bugs_test.hh"
#include "event_queue_test.hh"

#define CONCURRENT

int main (int argc, char *argv[]) {
    journal j;

    test *tests[] = {
        new start_stop_test (j),
        new start_stop_dir_test (j),
        new notifications_test (j),
        new notifications_dir_test (j),
        new update_flags_test (j),
        new update_flags_dir_test (j),
        new open_close_test (j),
        new symlink_test (j),
        new fail_test (j),
        new bugs_test (j),
        new event_queue_test (j),
    };
    const int num_tests = sizeof(tests)/sizeof(tests[0]);

#ifdef CONCURRENT
    for (int i = 0; i < num_tests; i++) {
        tests[i]->start ();
    }
    for (int i = 0; i < num_tests; i++) {
        tests[i]->wait_for_end ();
    }
#else
    for (int i = 0; i < num_tests; i++) {
        tests[i]->start ();
        tests[i]->wait_for_end ();
    }
#endif

    j.summarize ();

    for (int i = 0; i < num_tests; i++) {
        delete tests[i];
    }

    return 0;
}
