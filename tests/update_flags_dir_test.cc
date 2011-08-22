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

#include "update_flags_dir_test.hh"

update_flags_dir_test::update_flags_dir_test (journal &j)
: test ("Update directory flags", j)
{
}

void update_flags_dir_test::setup ()
{
    cleanup ();
    system ("mkdir ufdt-working");
    system ("touch ufdt-working/1");
}

void update_flags_dir_test::run ()
{
    consumer cons;
    int wid = 0;
    int new_wid = 0;
    events received;

    /* Add a watch */
    cons.input.setup ("ufdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("start watching successfully", wid != -1);


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ufdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive touch notifications for files in a directory",
            contains (received, event ("1", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ufdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("do not receive modify notifications for files in a directory without IN_MODIFY",
            received.empty ());


    cons.input.setup ("ufdt-working", IN_ATTRIB | IN_MODIFY);
    cons.output.wait ();

    new_wid = cons.output.added_watch_id ();
    should ("update flags successfully", wid == new_wid);


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ufdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive modify notifications for files in a directory with IN_MODIFY",
            contains (received, event ("1", wid, IN_MODIFY)));

    cons.input.interrupt ();
}

void update_flags_dir_test::cleanup ()
{
    system ("rm -rf ufdt-working");
}
