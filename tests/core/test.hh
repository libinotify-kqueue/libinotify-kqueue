#ifndef __TEST_HH__
#define __TEST_HH__

#include <pthread.h>
#include "platform.hh"
#include "journal.hh"

class test {
    journal::channel &jc;
    pthread_t thread;

protected:
    static void* run_ (void *ptr);

    virtual void setup () = 0;
    virtual void run () = 0;
    virtual void cleanup () = 0;

public:
    test (const std::string &name_, journal &j);
    virtual ~test ();

    void wait_for_end ();

    bool should (const std::string &test_name, bool exp);
    void pass (const std::string &test_name);
    void fail (const std::string &test_name);
};


#endif // __TEST_HH__
