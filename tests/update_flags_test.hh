#ifndef __UPDATE_FLAGS_HH__
#define __UPDATE_FLAGS_HH__

#include "core/core.hh"

class update_flags_test: public test {
protected:
    virtual void setup ();
    virtual void run ();
    virtual void cleanup ();

public:
    update_flags_test (journal &j);
};

#endif // __UPDATE_FLAGS_HH__
