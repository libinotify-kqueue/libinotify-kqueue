#ifndef __RESPONSE_HH__
#define __RESPONSE_HH__

#include "platform.hh"
#include "event.hh"
#include "action.hh"

class response: public action {
public:
    enum variant {
        REGISTERED_EVENTS,
        WATCH_ID,
    };

private:
    // Again, not a union
    struct {
        events _registered;
        int _watch_id;
    } variants;

    variant current;

public:
    response ();
    void setup (const events &registered);
    void setup (int watch_id);

    variant current_variant () const;
    events registered () const;
    int added_watch_id () const;
};

#endif // __RESPONSE_HH__
