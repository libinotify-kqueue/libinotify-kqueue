#include "notifications_test.hh"

/* Always present on Linux (not sure about a concrete 2.6.x release)
 * May be "to be implemented" on BSDs */
#ifndef IN_IGNORED
#  define IN_IGNORED	 0x00008000
#endif

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
    events expected;
    int wid = 0;

    /* Add a watch */
    cons.input.setup ("ntfst-working", IN_ALL_EVENTS);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();

    should ("watch is added successfully", wid != -1);

    /* These events are expected to occur on a touch */
    expected.insert (event ("", wid, IN_OPEN));
    expected.insert (event ("", wid, IN_ATTRIB));
    expected.insert (event ("", wid, IN_CLOSE_WRITE));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ntfst-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_OPEN on touch", !contains (expected, event ("", wid, IN_OPEN)));
    should ("receive IN_ATTRIB on touch", !contains (expected, event ("", wid, IN_ATTRIB)));
    should ("receive IN_CLOSE_WRITE on touch", !contains (expected, event ("", wid, IN_CLOSE_WRITE)));

    /* These events are expected to occur on a read */
    expected = events ();
    expected.insert (event ("", wid, IN_OPEN));
    expected.insert (event ("", wid, IN_CLOSE_NOWRITE));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("cat ntfst-working > /dev/null");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_OPEN on read", !contains (expected, event ("", wid, IN_OPEN)));
    should ("receive IN_CLOSE_NOWRITE on read", !contains (expected, event ("", wid, IN_CLOSE_NOWRITE)));

    /* These events are expected to occur on a write */
    expected = events ();
    expected.insert (event ("", wid, IN_OPEN));
    expected.insert (event ("", wid, IN_MODIFY));
    expected.insert (event ("", wid, IN_CLOSE_WRITE));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("echo Hello >> ntfst-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_OPEN on write", !contains (expected, event ("", wid, IN_OPEN)));
    should ("receive IN_MOFIFY on write", !contains (expected, event ("", wid, IN_MODIFY)));
    should ("receive IN_CLOSE_WRITE on write", !contains (expected, event ("", wid, IN_CLOSE_WRITE)));

    /* This event is expected to appear on move */
    expected = events ();
    expected.insert (event ("", wid, IN_MOVE_SELF));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("mv ntfst-working ntfst-working-2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_MOVE_SELF on move", !contains (expected, event ("", wid, IN_MOVE_SELF)));

    /* And these events are expected to appear on remove */
    expected = events ();
    expected.insert (event ("", wid, IN_DELETE_SELF));
    expected.insert (event ("", wid, IN_IGNORED));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("rm ntfst-working-2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_DELETE_SELF on remove", !contains (expected, event ("", wid, IN_DELETE_SELF)));
    should ("receive IN_IGNORED on remove", !contains (expected, event ("", wid, IN_IGNORED)));

    cons.input.interrupt ();
}

void notifications_test::cleanup ()
{
    system ("rm -rf ntfst-working");
}
