#ifndef __REQUEST_HH__
#define __REQUEST_HH__

#include "platform.hh"
#include "event.hh"
#include "action.hh"

class request: public action {
public:
    enum variant {
        REGISTER_ACTIVITY,
        ADD_MODIFY_WATCH,
        REMOVE_WATCH,
    };

    struct activity {
        events expected;
        int timeout;
    };

    struct add_modify {
        std::string path;
        uint32_t mask;
    };

    struct remove {
        int watch_id;
    };

private:
    // Not a union, because its members are non-POD
    struct {
        activity _act;
        add_modify _am;
        remove _rm;
    } variants;

    variant current;

public:
    request ();
    void setup (const events &expected, unsigned int timeout = 0);
    void setup (const std::string &path, uint32_t mask);
    void setup (int rm_id);

    variant current_variant () const;
    activity activity_data () const;
    add_modify add_modify_data () const;
    remove remove_data () const;
};

#endif // __REQUEST_HH__
