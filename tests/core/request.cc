#include <cassert>
#include "log.hh"
#include "request.hh"

request::request ()
: action ("REQUEST")
{
}

void request::setup (const events &expected, unsigned int timeout)
{
    LOG (named() << ": Setting up to register an activity");
    current = REGISTER_ACTIVITY;
    variants._act.expected = expected;
    variants._act.timeout = timeout;
    wait ();
}

void request::setup (const std::string &path, uint32_t mask)
{
    LOG (named() << ": Setting up to watch a path");
    current = ADD_MODIFY_WATCH;
    variants._am.path = path;
    variants._am.mask = mask;
    wait ();
}

void request::setup (int rm_id)
{
    LOG (named() << ": Setting up to stop a watch");
    current = REMOVE_WATCH;
    variants._rm.watch_id = rm_id;
    wait ();
}

request::variant request::current_variant () const
{
    return current;
}

request::activity request::activity_data () const
{
    assert (current == REGISTER_ACTIVITY);
    return variants._act;
}

request::add_modify request::add_modify_data () const
{
    assert (current == ADD_MODIFY_WATCH);
    return variants._am;
}

request::remove request::remove_data () const
{
    assert (current == REMOVE_WATCH);
    return variants._rm;
}

