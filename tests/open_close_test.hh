#ifndef __OPEN_CLOSE_TEST_HH__
#define __OPEN_CLOSE_TEST_HH__

#include "core/core.hh"

class open_close_test: public test {
protected:
    virtual void setup ();
    virtual void run ();
    virtual void cleanup ();

public:
    open_close_test (journal &j);
};

#endif
