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
    events expected;

    /* Add a watch */
    cons.input.setup ("uft-working", IN_ATTRIB);
    cons.output.wait ();
    wid = cons.output.added_watch_id ();
    should ("start watching successfully", wid != -1);
    
    /* Test if notifications work */
    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch uft-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive notifications on touch", expected.empty());

    /* Test if we do not receive notifications we not subscribed on */
    expected = events ();
    expected.insert (event ("", wid, IN_MODIFY));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("echo Hello >> uft-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("do not receive notifications on modify with flags = IN_ATTRIB",
            !expected.empty());

    /* Now update flags */
    cons.input.setup ("uft-working", IN_ATTRIB | IN_MODIFY);
    cons.output.wait ();
    updated_wid = cons.output.added_watch_id ();
    should ("modify flags updated successfully", wid == updated_wid);

    /* Now we should receive additional notifications on additional actions */
    expected = events ();
    expected.insert (event ("", wid, IN_MODIFY));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("echo Hello >> uft-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive notifications on modify with flags = IN_ATTRIB | IN_MODIFY",
            expected.empty());

    /* Test if touch notifications still work */
    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch uft-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive notifications on touch with flags = IN_ATTRIB | IN_MODIFY ",
            expected.empty());

    /* Now erase the original flag. Should not be notified about touches anymore. */
    cons.input.setup ("uft-working", IN_MODIFY);
    cons.output.wait ();
    updated_wid = cons.output.added_watch_id ();
    should ("modify flags updated successfully, again", wid == updated_wid);

    /* Now we should not receive notifications on touches */
    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch uft-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("do not receive notifications on touch with flags = IN_MODIFY ",
            !expected.empty());

    cons.input.interrupt ();
}

void update_flags_test::cleanup ()
{
    system ("rm -rf uft-working");
}
