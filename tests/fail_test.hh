#ifndef __FAIL_TEST_HH__
#define __FAIL_TEST_HH__

#include "core/core.hh"

class fail_test: public test {
protected:
    virtual void setup ();
    virtual void run ();
    virtual void cleanup ();

public:
    fail_test (journal &j);
};

#endif // __NOTIFICAIONTS_TEST_HH__
