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

    /* Add a watch */
    cons.input.setup ("sst-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();

    should ("watch is added successfully", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    events expected;
    expected.insert (event ("", wid, IN_ATTRIB));
    cons.output.reset ();
    cons.input.setup (expected, 1);

    /* Produce activity */
    system ("touch sst-working");

    cons.output.wait ();

    expected = cons.output.left_unregistered ();
    should ("all produced events are registered", expected.empty ());

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
    
    /* Tell again to consumer to watch for an IN_ATTRIB event  */
    expected = events();
    expected.insert (event ("", wid, IN_ATTRIB));
    cons.output.reset ();
    cons.input.setup (expected, 1);
    
    /* Produce activity. Consumer should not see it */
    system ("touch sst-working");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("events should not be registered on a removed watch", expected.size() == 1);

    /* Now start watching again. Everything should work */
    cons.input.setup ("sst-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();

    should ("start watching a file after stop", wid != -1);

    /* Tell consumer to watch for an IN_ATTRIB event */
    expected = events ();
    expected.insert (event ("", wid, IN_ATTRIB));
    cons.output.reset ();
    cons.input.setup (expected, 1);

    /* Produce activity, consumer should watch it */
    system ("touch sst-working");

    cons.output.wait ();

    expected = cons.output.left_unregistered ();
    should ("all produced events are registered after resume", expected.empty ());

    cons.input.interrupt ();
}

void start_stop_test::cleanup ()
{
    system ("rm -rf sst-working");
}
