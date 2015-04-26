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
#include "fail_test.hh"

fail_test::fail_test (journal &j)
: test ("Failures", j)
{
}

void fail_test::setup ()
{
    cleanup ();
    system ("touch fail-working");
}

void fail_test::run ()
{
    consumer cons;
    int wid = 0;

    /* Add a watch, should fail */
    cons.input.setup ("non-existent", IN_ALL_EVENTS);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch id is -1 when starting watching a non-existent file", wid == -1);

    cons.output.reset ();
    cons.input.setup ("fail-working", IN_ATTRIB | IN_ONLYDIR);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("do not start watching a file if IN_ONLYDIR flag is set", wid == -1);

    cons.output.reset ();
    cons.input.setup ("fail-working", 0);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("do not start watching a file if no event flags are set", wid == -1);

    cons.input.interrupt ();
}

void fail_test::cleanup ()
{
    system ("rm -rf fail-working");
}
