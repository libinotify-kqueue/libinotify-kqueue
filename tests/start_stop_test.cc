#include <cstdlib>
#include "start_stop_test.hh"

/* Always present on Linux (not sure about a concrete 2.6.x release)
 * May be "to be implemented" on BSDs */
#ifndef IN_IGNORED
#  define IN_IGNORED	 0x00008000
#endif

start_stop_test::start_stop_test (journal &j)
: test ("Start-stop test", j)
{
}

void start_stop_test::setup ()
{
    cleanup ();
    system ("touch sst-working");
}

void start_stop_test::run ()
{
    consumer cons;
    int wid = 0;
    events received;

    /* Add a watch */
    cons.input.setup ("sst-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch is added successfully", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    cons.input.receive ();

    system ("touch sst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("all produced events are registered",
            contains (received, event ("", wid, IN_ATTRIB)));

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
    
    /* Tell again to consumer to watch for an IN_ATTRIB event  */
    cons.output.reset ();
    cons.input.receive ();
    
    system ("touch sst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("events should not be registered on a removed watch",
            received.size() == 0);

    /* Now start watching again. Everything should work */
    cons.input.setup ("sst-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("start watching a file after stop", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    cons.output.reset ();
    cons.input.receive ();

    /* Produce activity, consumer should watch it */
    system ("touch sst-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("all produced events are registered after resume",
            contains (received, event ("", wid, IN_ATTRIB)));

    cons.input.interrupt ();
}

void start_stop_test::cleanup ()
{
    system ("rm -rf sst-working");
}
