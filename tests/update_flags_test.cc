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

    cons.input.interrupt ();
}

void update_flags_test::cleanup ()
{
    system ("rm -rf uft-working");
}
