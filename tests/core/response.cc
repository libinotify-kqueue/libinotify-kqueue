#include <cassert>
#include "log.hh"
#include "response.hh"

response::response ()
: action ("RESPONSE")
{
}

void response::setup (const events &registered)
{
    LOG (named() << ": Passing back unregistered events");
    current = REGISTERED_EVENTS;
    variants._registered = registered;
    wait ();
    LOG (named() << " YAY!!!");
}

void response::setup (int watch_id)
{
    LOG (named() << ": Passing back new watch id");
    current = WATCH_ID;
    variants._watch_id = watch_id;
    wait ();
}

response::variant response::current_variant () const
{
    return current;
}

events response::registered () const
{
    assert (current == REGISTERED_EVENTS);
    return variants._registered;
}

int response::added_watch_id () const
{
    assert (current == WATCH_ID);
    return variants._watch_id;
}

