#include <algorithm>
#include "notifications_dir_test.hh"

/* Always present on Linux (not sure about a concrete 2.6.x release)
 * May be "to be implemented" on BSDs */
#ifndef IN_IGNORED
#  define IN_IGNORED	 0x00008000
#endif

#ifndef IN_ISDIR
#  define IN_ISDIR	     0x40000000
#endif

notifications_dir_test::notifications_dir_test (journal &j)
: test ("Directory notifications", j)
{
}

void notifications_dir_test::setup ()
{
    cleanup ();
    system ("mkdir ntfsdt-working");
}

void notifications_dir_test::run ()
{
    consumer cons;
    events expected;
    int wid = 0;

    /* Add a watch */
    cons.input.setup ("ntfsdt-working", IN_ALL_EVENTS);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);
    
    /* These events are expected to appear on a touch */
    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB | IN_ISDIR));
    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ntfsdt-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_ATTRIB with IN_ISDIR flag set on touch on a directory", expected.empty ());

    /* These events are expected to appear on a listing */
    expected = events ();
    expected.insert (event ("", wid, IN_OPEN | IN_ISDIR));
    expected.insert (event ("", wid, IN_CLOSE_NOWRITE | IN_ISDIR));
    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("ls ntfsdt-working > /dev/null");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_OPEN with IN_ISDIR flag set on listing on a directory",
            !contains (expected, event ("", wid, IN_OPEN | IN_ISDIR)));
    should ("receive IN_CLOSE_NOWRITE with IN_ISDIR flag set on listing on a directory",
            !contains (expected, event ("", wid, IN_CLOSE_NOWRITE | IN_ISDIR)));

    /* These events are expected to appear on creating an entry in a directory (with touch) */
    expected = events ();
    expected.insert (event ("1", wid, IN_CREATE));
    expected.insert (event ("1", wid, IN_OPEN));
    expected.insert (event ("1", wid, IN_ATTRIB));
    expected.insert (event ("1", wid, IN_CLOSE_WRITE));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ntfsdt-working/1");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_CREATE event for a new entry in a directory (created with touch)",
            !contains (expected, event ("1", wid, IN_CREATE)));
    should ("receive IN_OPEN event for a new entry in a directory (created with touch)",
            !contains (expected, event ("1", wid, IN_OPEN)));
    should ("receive IN_ATTRIB event for a new entry in a directory (created with touch)",
            !contains (expected, event ("1", wid, IN_ATTRIB)));
    should ("receive IN_CLOSE_WRITE event for a new entry in a directory (created with touch)",
            !contains (expected, event ("1", wid, IN_CLOSE_WRITE)));

    /* These events are expected to appear on creating an entry in a directory (with echo) */
    expected = events ();
    expected.insert (event ("2", wid, IN_CREATE));
    expected.insert (event ("2", wid, IN_OPEN));
    expected.insert (event ("2", wid, IN_MODIFY));
    expected.insert (event ("2", wid, IN_CLOSE_WRITE));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("echo Hello >> ntfsdt-working/2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_CREATE event for a new entry in a directory (created with echo)",
            !contains (expected, event ("2", wid, IN_CREATE)));
    should ("receive IN_OPEN event for a new entry in a directory (created with echo)",
            !contains (expected, event ("2", wid, IN_OPEN)));
    should ("receive IN_MODIFY event for a new entry in a directory (created with echo)",
            !contains (expected, event ("2", wid, IN_MODIFY)));
    should ("receive IN_CLOSE_WRITE event for a new entry in a directory (created with echo)",
            !contains (expected, event ("2", wid, IN_CLOSE_WRITE)));

    /* This event is expected to appear on deleting an entry from a directory */
    expected = events ();
    expected.insert (event ("2", wid, IN_DELETE));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("rm ntfsdt-working/2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_DELETE event on deleting a file from a directory", expected.empty ());

#ifdef TESTS_MOVES_TRICKY
    /* And tricky test case to test renames in a directory.
     *
     * I am asking consumer to watch for event that will never appear.
     * However, I still will produce activity and consumer will continue registering it.
     *
     * IN_MOVED_FROM and IN_MOVED_TO can be tested as usual, but I also want to
     * check event cookies (see inotify documentation for details), so I want to deal
     * with all received notifications here, not just with unregistered ones.
     *
     * TODO: This test is so strict so it even fails on Linux. I think there is a error
     * somewhere, so another version of the test is used by default.
     */
    expected = events ();
    expected.insert (event ("NOMATCH", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 5);

    system ("mv ntfsdt-working/1 ntfsdt-working/one");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    events::iterator iter_from, iter_to;
    iter_from = std::find_if (expected.begin(),
                              expected.end(),
                              event_matcher (event ("1", wid, IN_MOVED_FROM)));
    iter_to = std::find_if (expected.begin(),
                            expected.end(),
                            event_matcher (event ("one", wid, IN_MOVED_TO)));

    if (should ("receive both IN_MOVED_FROM and IN_MOVED_TO for rename",
                iter_from != expected.end () && iter_to != expected.end())) {
        should ("both events for a rename have the same cookie",
                iter_from->cookie == iter_to->cookie);
    }
#esle
    /* A simplier and less strict version */
    expected = events ();
    expected.insert (event ("1", wid, IN_MOVED_FROM));
    expected.insert (event ("one", wid, IN_MOVED_TO));

    cons.output.reset ();
    cons.input.setup (expected, 5);

    system ("mv ntfsdt-working/1 ntfsdt-working/one");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive all events on moves", expected.empty ());
#endif

    /* These events are expected to appear on modifying an entry in a directory */
    expected = events ();
    expected.insert (event ("one", wid, IN_OPEN));
    expected.insert (event ("one", wid, IN_MODIFY));
    expected.insert (event ("one", wid, IN_CLOSE_WRITE));

    cons.output.reset ();
    cons.input.setup (expected, 2);

    system ("echo Hello >> ntfsdt-working/one");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_OPEN event on modifying an entry in a directory",
            !contains (expected, event ("one", wid, IN_OPEN)));
    should ("receive IN_ATTRIB event on modifying an entry in a directory",
            !contains (expected, event ("one", wid, IN_MODIFY)));
    should ("receive IN_CLOSE_WRITE event on modifying an entry in a directory",
            !contains (expected, event ("one", wid, IN_CLOSE_WRITE)));
    
    /* This event is expected to appear on a rename */
    expected = events ();
    expected.insert (event ("", wid, IN_MOVE_SELF));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("mv ntfsdt-working ntfsdt-working-2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive a move events", expected.empty ());

    /* This event is expected to appear on remove */
    expected = events ();
    expected.insert (event ("", wid, IN_OPEN));
    expected.insert (event ("one", wid, IN_DELETE));
    expected.insert (event ("", wid, IN_CLOSE_WRITE));
    expected.insert (event ("", wid, IN_DELETE_SELF));
    expected.insert (event ("", wid, IN_IGNORED));

    cons.output.reset ();
    cons.input.setup (expected, 4);

    system ("rm -rf ntfsdt-working-2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();
    should ("receive IN_OPEN on removing a directory",
            !contains (expected, event ("", wid, IN_OPEN)));
    should ("receive IN_DELETE for a file in a directory on removing a directory",
            !contains (expected, event ("one", wid, IN_OPEN)));
    should ("receive IN_CLOSE_WRITE on removing a directory",
            !contains (expected, event ("", wid, IN_CLOSE_WRITE)));
    should ("receive IN_DELETE_SELF on removing a directory",
            !contains (expected, event ("", wid, IN_DELETE_SELF)));
    should ("receive IN_IGNORED on removing a directory",
            !contains (expected, event ("", wid, IN_IGNORED)));
    
    cons.input.interrupt ();
}

void notifications_dir_test::cleanup ()
{
    system ("rm -rf ntfsdt-working-2");
    system ("rm -rf ntfsdt-working");
}
