#include "open_close_test.hh"

open_close_test::open_close_test (journal &j)
: test ("Open/close notifications", j)
{
}

void open_close_test::setup ()
{
    cleanup ();
    system ("touch oct-file-working");
    system ("echo Hello >> oct-file-working");
    system ("mkdir oct-dir-working");
}

void open_close_test::run ()
{
    consumer cons;
    int file_wid = 0;
    int dir_wid = 0;
    events received;

    cons.input.setup ("oct-file-working", IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
    cons.output.wait ();

    file_wid = cons.output.added_watch_id ();
    should ("start watching on a file", file_wid != -1);


    cons.output.reset ();
    cons.input.setup ("oct-dir-working", IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
    cons.output.wait ();

    dir_wid = cons.output.added_watch_id ();
    should ("start watching on a directory", dir_wid != -1);

    cons.output.reset ();
    cons.input.receive ();

    system ("cat oct-file-working >> /dev/null");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_OPEN on cat",
            contains (received, event ("", file_wid, IN_OPEN)));
    should ("receive IN_CLOSE_NOWRITE on cat",
            contains (received, event ("", file_wid, IN_CLOSE_NOWRITE)));


    cons.output.reset ();
    cons.input.receive ();

    system ("ls oct-dir-working >> /dev/null");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_OPEN on ls",
            contains (received, event ("", dir_wid, IN_OPEN)));
    should ("receive IN_CLOSE_NOWRITE on ls",
            contains (received, event ("", dir_wid, IN_CLOSE_NOWRITE)));


    cons.output.reset ();
    cons.input.receive ();

    system ("echo Hello >> oct-file-working");

    cons.output.wait ();
    received = cons.output.registered ();
    should ("receive IN_OPEN on modify",
            contains (received, event ("", file_wid, IN_MODIFY)));
    should ("receive IN_CLOSE_WRITE on modify",
            contains (received, event ("", file_wid, IN_CLOSE_WRITE)));

    
    cons.input.interrupt ();
}

void open_close_test::cleanup ()
{
    system ("rm -rf oct-file-working");
    system ("rm -rf oct-dir-working");
}
