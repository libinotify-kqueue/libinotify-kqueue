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

#include "notifications_test.hh"

notifications_test::notifications_test (journal &j)
: test ("File notifications", j)
{
}

void notifications_test::setup ()
{
    cleanup ();
    system ("touch ntfst-working");
}

void notifications_test::run ()
{
    consumer cons;
    events received;
    int wid = 0;


    cons.input.setup ("ntfst-working", IN_ATTRIB | IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);


    cons.output.reset ();
    cons.input.receive (2);

    system ("touch ntfst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_ATTRIB on touch", contains (received, event ("", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ntfst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_MOFIFY on write", contains (received, event ("", wid, IN_MODIFY)));


    cons.output.reset ();
    cons.input.receive ();

    system ("mv ntfst-working ntfst-working-2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_MOVE_SELF on move", contains (received, event ("", wid, IN_MOVE_SELF)));

    
    cons.output.reset ();
    cons.input.receive ();

    system ("rm ntfst-working-2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_DELETE_SELF on remove", contains (received, event ("", wid, IN_DELETE_SELF)));
    should ("receive IN_IGNORED on remove", contains (received, event ("", wid, IN_IGNORED)));

    cons.input.interrupt ();
}

void notifications_test::cleanup ()
{
    system ("rm -rf ntfst-working");
}
