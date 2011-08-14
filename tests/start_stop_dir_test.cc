#include <cstdlib>
#include "start_stop_dir_test.hh"

/* Always present on Linux (not sure about a concrete 2.6.x release)
 * May be "to be implemented" on BSDs */
#ifndef IN_IGNORED
#  define IN_IGNORED	 0x00008000
#endif

start_stop_dir_test::start_stop_dir_test (journal &j)
: test ("Start-stop directory", j)
{
}

void start_stop_dir_test::setup ()
{
    cleanup ();
    
    system ("mkdir ssdt-working");
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");
}

void start_stop_dir_test::run ()
{
    consumer cons;
    events expected;
    int wid = 0;

    /* Add a watch */
    cons.input.setup ("ssdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();

    should ("watch is added successfully", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    expected.insert (event ("", wid, IN_ATTRIB));
    cons.output.reset ();
    cons.input.setup (expected, 1);

    /* Produce activity */
    system ("touch ssdt-working");

    cons.output.wait ();

    expected = cons.output.left_unregistered ();
    should ("events are registered on a directory", expected.empty ());

    /* Watch should also signal us about activity on files at the watched directory. */
    expected = events ();
    expected.insert (event ("1", wid, IN_ATTRIB));
    expected.insert (event ("2", wid, IN_ATTRIB));
    expected.insert (event ("3", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    /* This events should be registered */
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");
    
    cons.output.wait ();

    expected = cons.output.left_unregistered ();
    should ("events are registered on the directory contents", expected.empty ());

    /* Now stop watching */
    cons.output.reset ();
    cons.input.setup (wid);
    cons.output.wait ();

    /* Linux inotify sends IN_IGNORED on stop */
    expected = events ();
    expected.insert (event ("", wid, IN_IGNORED));

    cons.output.reset ();
    cons.input.setup (expected, 1);
    cons.output.wait ();

    expected = cons.output.left_unregistered ();
    should ("got IN_IGNORED on watch stop", expected.empty ());

    /* These events should not be visible to watch */
    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB));
    expected.insert (event ("2", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ssdt-working");
    system ("touch ssdt-working/2");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("items on a stopped watch are unregistered", expected.size() == 2);

    /* Now resume watching */
    cons.output.reset ();
    cons.input.setup ("ssdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();

    should ("watch is added successfully again", wid != -1);

    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB));
    expected.insert (event ("1", wid, IN_ATTRIB));
    expected.insert (event ("2", wid, IN_ATTRIB));
    expected.insert (event ("3", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ssdt-working");
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("receive all events on a resumed watch", expected.empty());

    /* Now, start watching on a file from a directory manually */
    int child_wid = 0;

    cons.output.reset ();
    cons.input.setup ("ssdt-working/3", IN_ATTRIB);
    cons.output.wait ();

    child_wid = cons.output.added_watch_id ();

    should ("watch on a file in a directory is added successfully", wid != -1);

    /* On a single touch, should recive two events */
    expected = events ();
    expected.insert (event ("3", wid, IN_ATTRIB));
    expected.insert (event ("", child_wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ssdt-working/3");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("receive events for a same file from both watches "
            "(sometimes this test fails event on Linux, at least on 2.6.39)",
            expected.empty());

    /* Now stop a directory watch */
    cons.output.reset ();
    cons.input.setup (wid);
    cons.output.wait ();

    /* Linux inotify sends IN_IGNORED on stop */
    expected = events ();
    expected.insert (event ("", wid, IN_IGNORED));

    cons.output.reset ();
    cons.input.setup (expected, 1);
    cons.output.wait ();

    /* Still should be able to receive notifications from an additional watch */
    expected = events ();
    expected.insert (event ("3", wid, IN_ATTRIB));
    expected.insert (event ("", child_wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ssdt-working/3");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("after stop on a directory watch, "
            "receive only a single event from a file watch",
            expected.size() == 1 && expected.begin()->watch == wid);

    cons.input.interrupt ();
}

void start_stop_dir_test::cleanup ()
{
    system ("rm -rf ssdt-working");
}
