#ifndef __RESPONSE_HH__
#define __RESPONSE_HH__

#include "platform.hh"
#include "event.hh"
#include "action.hh"

class response: public action {
public:
    enum variant {
        UNREGISTERED_EVENTS,
        WATCH_ID,
    };

private:
    // Again, not a union
    struct {
        events _left_unreg;
        int _watch_id;
    } variants;

    variant current;

public:
    response ();
    void setup (const events &unregistered);
    void setup (int watch_id);

    variant current_variant () const;
    events left_unregistered () const;
    int added_watch_id () const;
};

#endif // __RESPONSE_HH__
