#ifndef __UPDATE_FLAGS_DIR_TEST_HH__
#define __UPDATE_FLAGS_DIR_TEST_HH__

#include "core/core.hh"

class update_flags_dir_test: public test {
protected:
    virtual void setup ();
    virtual void run ();
    virtual void cleanup ();

public:
    update_flags_dir_test (journal &j);
};

#endif //  __UPDATE_FLAGS_DIR_TEST_HH__
