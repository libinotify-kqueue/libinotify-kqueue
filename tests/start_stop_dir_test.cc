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
    events received;
    int wid = 0;

    /* Add a watch */
    cons.input.setup ("ssdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    cons.output.reset ();
    cons.input.receive ();

    /* Produce activity */
    system ("touch ssdt-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("events are registered on a directory",
            contains (received, event ("", wid, IN_ATTRIB)));

    /* Watch should also signal us about activity on files at the watched directory. */
    cons.output.reset ();
    cons.input.receive (1);

    /* This events should be registered */
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");
    
    cons.output.wait ();
    received = cons.output.registered ();
    should ("events are registered on the directory contents",
            contains (received, event ("1", wid, IN_ATTRIB))
            && contains (received, event ("2", wid, IN_ATTRIB))
            && contains (received, event ("3", wid, IN_ATTRIB)));

    /* Now stop watching */
    cons.output.reset ();
    cons.input.setup (wid);
    cons.output.wait ();

    /* Linux inotify sends IN_IGNORED on stop */
    cons.output.reset ();
    cons.input.receive ();
    
    cons.output.wait ();
    received = cons.output.registered ();
    should ("got IN_IGNORED on watch stop",
            contains (received, event ("", wid, IN_IGNORED)));

    /* These events should not be visible to watch */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working");
    system ("touch ssdt-working/2");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("items on a stopped watch are unregistered", received.empty ());

    /* Now resume watching */
    cons.output.reset ();
    cons.input.setup ("ssdt-working", IN_ATTRIB);

    cons.output.wait ();
    wid = cons.output.added_watch_id ();
    should ("watch is added successfully again", wid != -1);

    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working");
    system ("touch ssdt-working/1");
    system ("touch ssdt-working/2");
    system ("touch ssdt-working/3");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive all events on a resumed watch",
            contains (received, (event ("", wid, IN_ATTRIB)))
            && contains (received, (event ("1", wid, IN_ATTRIB)))
            && contains (received, (event ("2", wid, IN_ATTRIB)))
            && contains (received, (event ("3", wid, IN_ATTRIB))));

    /* Now, start watching on a file from a directory manually */
    int child_wid = 0;

    cons.output.reset ();
    cons.input.setup ("ssdt-working/3", IN_ATTRIB);

    cons.output.wait ();
    child_wid = cons.output.added_watch_id ();
    should ("watch on a file in a directory is added successfully", wid != -1);

    /* On a single touch, should recive two events */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working/3");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive events for a same file from both watches "
            "(sometimes this test fails event on Linux, at least on 2.6.39)",
            contains (received, event ("3", wid, IN_ATTRIB))
            && contains (received, event ("", child_wid, IN_ATTRIB)));

    /* Now stop a directory watch */
    cons.output.reset ();
    cons.input.setup (wid);
    cons.output.wait ();

    /* Linux inotify sends IN_IGNORED on stop */
    cons.output.reset ();
    cons.input.receive ();
    cons.output.wait ();

    /* Still should be able to receive notifications from an additional watch */
    cons.output.reset ();
    cons.input.receive ();

    system ("touch ssdt-working/3");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("after stop on a directory watch, "
            "receive only a single event from a file watch",
            contains (received, event ("", child_wid, IN_ATTRIB)));

    cons.input.interrupt ();
}

void start_stop_dir_test::cleanup ()
{
    system ("rm -rf ssdt-working");
}
