/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*******************************************************************************/

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

    return (ev.filename == ev_.filename
            && ev.watch == ev_.watch
            && (ev.flags & ev_.flags));
}


bool contains (const events &ev, const event &ev_)
{
    event_matcher matcher (ev_);
    events::iterator iter = std::find_if (ev.begin(), ev.end(), matcher);
    return (iter != ev.end());
}
