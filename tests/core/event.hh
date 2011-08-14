#ifndef __EVENT_HH__
#define __EVENT_HH__

#include <set>
#include "platform.hh"

struct event {
    std::string filename;
    int watch;
    uint32_t flags;
    uint32_t cookie;

    event (const std::string &filename_ = "", int watch_ = 0, uint32_t flags_ = 0);
    bool operator< (const event &ev) const;
};

typedef std::set<event> events;

class event_matcher {
    event ev;

public:
    event_matcher (const event &ev_);
    bool operator() (const event &ev_) const;
};

bool contains (const events &ev, const event &ev_);

#endif // __EVENT_HH__
