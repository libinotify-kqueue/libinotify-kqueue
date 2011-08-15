#include <algorithm>
#include "event.hh"
#include "log.hh"

event::event (const std::string &filename_, int watch_, uint32_t flags_)
: filename (filename_)
, watch (watch_)
, flags (flags_)
, cookie (0)
{
}

bool event::operator< (const event &ev) const
{
    if (watch == ev.watch) {
        if (filename == ev.filename) {
            return flags < ev.flags;
        } else {
            return filename < ev.filename;
        }
    } else {
        return watch < ev.watch;
    }
}

event_matcher::event_matcher (const event &ev_)
: ev(ev_)
{
    LOG ("Created matcher " << ev.filename << ':' << ev.watch);
}

bool event_matcher::operator() (const event &ev_) const
{
    LOG ("matching " << ev.filename  << ':' << ev.watch  << '&' << ev.flags <<
         " against " << ev_.filename << ':' << ev_.watch << '&' << ev_.flags);

    return
        (ev.filename == ev_.filename
         && ev.watch == ev_.watch
         && (ev.flags & ev_.flags));
}


bool contains (const events &ev, const event &ev_)
{
    event_matcher matcher (ev_);
    events::iterator iter = std::find_if (ev.begin(), ev.end(), matcher);
    return (iter != ev.end());
}
