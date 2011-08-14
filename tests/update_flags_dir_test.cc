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
    events expected;

    /* Add a watch */
    cons.input.setup ("ufdt-working", IN_ATTRIB);
    cons.output.wait ();

    wid = cons.output.added_watch_id ();
    should ("start watching successfully", wid != -1);

    /* Test notifications on touches in the directory */
    expected = events ();
    expected.insert (event ("1", wid, IN_ATTRIB));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("touch ufdt-working/1");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("receive touch notifications for files in a directory", !expected.size());

    /* Test notifications on modifications in the directory without IN_MODIFY */
    expected = events ();
    expected.insert (event ("1", wid, IN_MODIFY));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("echo Hello >> ufdt-working/1");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("do not receive modify notifications for files in a directory without IN_MODIFY",
            expected.size() > 0);

    /* Update flags */
    cons.input.setup ("ufdt-working", IN_ATTRIB | IN_MODIFY);
    cons.output.wait ();

    new_wid = cons.output.added_watch_id ();
    should ("update flags successfully", wid == new_wid);

    /* Test if new notifications are coming */
    expected = events ();
    expected.insert (event ("1", wid, IN_MODIFY));

    cons.output.reset ();
    cons.input.setup (expected, 1);

    system ("echo Hello >> ufdt-working/1");

    cons.output.wait ();
    expected = cons.output.left_unregistered ();

    should ("receive modify notifications for files in a directory with IN_MODIFY",
            !expected.size());

    cons.input.interrupt ();
}

void update_flags_dir_test::cleanup ()
{
    system ("rm -rf ufdt-working");
}
