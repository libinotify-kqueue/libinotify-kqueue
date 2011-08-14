#include "fail_test.hh"

fail_test::fail_test (journal &j)
: test ("Failures", j)
{
}

void fail_test::setup ()
{
}

void fail_test::run ()
{
    consumer cons;
    int wid = 0;

    /* Add a watch, should fail */
    cons.input.setup ("fail-working", IN_ALL_EVENTS);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("watch id is -1 when starting watching a non-existent file", wid == -1);

    cons.input.interrupt ();
}

void fail_test::cleanup ()
{
}
