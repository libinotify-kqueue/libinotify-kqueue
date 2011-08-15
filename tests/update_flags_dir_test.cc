#include "update_flags_dir_test.hh"

update_flags_dir_test::update_flags_dir_test (journal &j)
: test ("Update directory flags", j)
{
}

void update_flags_dir_test::setup ()
{
    cleanup ();
    system ("mkdir ufdt-working");
    system ("touch ufdt-working/1");
}

void update_flags_dir_test::run ()
{
    consumer cons;
    int wid = 0;
    int new_wid = 0;
    events received;

    /* Add a watch */
    cons.input.setup ("ufdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("start watching successfully", wid != -1);


    cons.output.reset ();
    cons.input.receive ();

    system ("touch ufdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive touch notifications for files in a directory",
            contains (received, event ("1", wid, IN_ATTRIB)));


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ufdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("do not receive modify notifications for files in a directory without IN_MODIFY",
            received.empty ());


    cons.input.setup ("ufdt-working", IN_ATTRIB | IN_MODIFY);
    cons.output.wait ();

    new_wid = cons.output.added_watch_id ();
    should ("update flags successfully", wid == new_wid);


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> ufdt-working/1");

    cons.output.wait ();
    received = cons.output.registered ();

    should ("receive modify notifications for files in a directory with IN_MODIFY",
            contains (received, event ("1", wid, IN_MODIFY)));

    cons.input.interrupt ();
}

void update_flags_dir_test::cleanup ()
{
    system ("rm -rf ufdt-working");
}
