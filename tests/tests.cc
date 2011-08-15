#include "start_stop_test.hh"
#include "start_stop_dir_test.hh"
#include "notifications_test.hh"
#include "notifications_dir_test.hh"
#include "fail_test.hh"
#include "update_flags_test.hh"
#include "update_flags_dir_test.hh"
#include "open_close_test.hh"

#define THREADED

int main (int argc, char *argv[]) {
    journal j;

#ifdef THREADED
    start_stop_test sst (j);
    start_stop_dir_test ssdt (j);
    notifications_test ntfst (j);
    notifications_dir_test ntfsdt (j);
    fail_test ft (j);
    update_flags_test uft (j);
    update_flags_dir_test ufdt (j);
    open_close_test oct (j);
    
    sst.wait_for_end ();
    ssdt.wait_for_end ();
    ntfst.wait_for_end ();
    ntfsdt.wait_for_end ();
    ft.wait_for_end ();
    uft.wait_for_end ();
    ufdt.wait_for_end ();
    oct.wait_for_end ();
#else
    start_stop_test sst (j);
    sst.wait_for_end ();

    start_stop_dir_test ssdt (j);
    ssdt.wait_for_end ();

    notifications_test ntfst (j);
    ntfst.wait_for_end ();

    notifications_dir_test ntfsdt (j);
    ntfsdt.wait_for_end ();

    fail_test ft (j);
    ft.wait_for_end ();

    update_flags_test uft (j);
    uft.wait_for_end ();

    update_flags_dir_test ufdt (j);
    ufdt.wait_for_end ();

    open_close_test oct (j);
    oct.wait_for_end ();
#endif

    j.summarize ();
    return 0;
}
