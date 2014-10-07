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

#include "update_flags_test.hh"

update_flags_test::update_flags_test (journal &j)
: test ("Update watch flags", j)
{
}

void update_flags_test::setup ()
{
    cleanup ();
    system ("touch uft-working");
}

void update_flags_test::run ()
{
    consumer cons;
    int wid = 0;
    int updated_wid = 0;
    events received;

    /* Add a watch */
    cons.input.setup ("uft-working", IN_ATTRIB);
    cons.output.wait ();
    
    wid = cons.output.added_watch_id ();
    should ("start watching successfully", wid != -1);
    

    cons.output.reset ();
    cons.input.receive ();

    system ("touch uft-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive notifications on touch",
            contains (received, event ("", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> uft-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("do not receive notifications on modify with flags = IN_ATTRIB",
            received.empty());


    cons.output.reset ();
    cons.input.setup ("uft-working", IN_ATTRIB | IN_MODIFY);
    cons.output.wait ();

    updated_wid = cons.output.added_watch_id ();
    should ("modify flags updated successfully", wid == updated_wid);


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> uft-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive notifications on modify with flags = IN_ATTRIB | IN_MODIFY",
            contains (received, event ("", wid, IN_MODIFY)));


    cons.output.reset ();
    cons.input.receive ();

    system ("touch uft-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive notifications on touch with flags = IN_ATTRIB | IN_MODIFY ",
            contains (received, event ("", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.setup ("uft-working", IN_MODIFY);
    cons.output.wait ();
    updated_wid = cons.output.added_watch_id ();
    should ("modify flags updated successfully, again", wid == updated_wid);


    cons.output.reset ();
    cons.input.receive ();

    system ("touch uft-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("do not receive notifications on touch with flags = IN_MODIFY ",
            received.empty());


    cons.output.reset ();
    cons.input.setup ("uft-working", IN_ATTRIB | IN_MASK_ADD);
    cons.output.wait ();
    updated_wid = cons.output.added_watch_id ();
    should ("modify flags updated successfully, again", wid == updated_wid);


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> uft-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive notifications on modify after watch with IN_MODIFY flag "
            "set has been updated with IN_MASK_ADD set and IN_MODIFY unset",
            contains (received, event ("", updated_wid, IN_MODIFY)));

    cons.input.interrupt ();
}

void update_flags_test::cleanup ()
{
    system ("rm -rf uft-working");
}
