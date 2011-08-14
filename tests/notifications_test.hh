#ifndef __NOTIFICAIONTS_TEST_HH__
#define __NOTIFICAIONTS_TEST_HH__

#include "core/core.hh"

class notifications_test: public test {
protected:
    virtual void setup ();
    virtual void run ();
    virtual void cleanup ();

public:
    notifications_test (journal &j);
};

#endif // __NOTIFICAIONTS_TEST_HH__
