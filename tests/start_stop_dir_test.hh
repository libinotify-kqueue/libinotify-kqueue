#ifndef __START_STOP_DIR_TEST_HH__
#define __START_STOP_DIR_TEST_HH__

#include "core/core.hh"

class start_stop_dir_test: public test {
protected:
    virtual void setup ();
    virtual void run ();
    virtual void cleanup ();

public:
    start_stop_dir_test (journal &j);
};

#endif // __START_STOP_DIR_TEST_HH__
