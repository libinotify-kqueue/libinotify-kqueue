#ifndef __ACTION_HH__
#define __ACTION_HH__

#include "platform.hh"

class action {
    void init();

    pthread_mutex_t action_mutex;
    pthread_mutex_t cond_mutex;
    pthread_cond_t cond;

    const std::string name;
    volatile bool interrupted;
    volatile bool waiting;

public:
    action (const std::string &name_);
    virtual ~action () = 0;

    bool wait ();
    void interrupt ();
    void reset ();

    std::string named () const;
};

#endif // __ACTION_HH__
