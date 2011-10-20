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

#include <stdlib.h>
#include "start_stop_test.hh"
#include "start_stop_dir_test.hh"
#include "notifications_test.hh"
#include "notifications_dir_test.hh"
#include "fail_test.hh"
#include "update_flags_test.hh"
#include "update_flags_dir_test.hh"
#include "open_close_test.hh"

#define THREADED

int main (int argc, char *argv[]) {
    journal j;

#ifdef THREADED
    start_stop_test sst (j);
    start_stop_dir_test ssdt (j);
    notifications_test ntfst (j);
    notifications_dir_test ntfsdt (j);
    update_flags_test uft (j);
    update_flags_dir_test ufdt (j);
    open_close_test oct (j);
    fail_test ft (j);
    
    sst.wait_for_end ();
    ssdt.wait_for_end ();
    ntfst.wait_for_end ();
    ntfsdt.wait_for_end ();
    uft.wait_for_end ();
    ufdt.wait_for_end ();
    oct.wait_for_end ();
    ft.wait_for_end ();
#else
    start_stop_test sst (j);
    sst.wait_for_end ();

    start_stop_dir_test ssdt (j);
    ssdt.wait_for_end ();

    notifications_test ntfst (j);
    ntfst.wait_for_end ();

    notifications_dir_test ntfsdt (j);
    ntfsdt.wait_for_end ();

    update_flags_test uft (j);
    uft.wait_for_end ();

    update_flags_dir_test ufdt (j);
    ufdt.wait_for_end ();

    open_close_test oct (j);
    oct.wait_for_end ();
  
    fail_test ft (j);
    ft.wait_for_end ();
#endif

    j.summarize ();
    return 0;
}
