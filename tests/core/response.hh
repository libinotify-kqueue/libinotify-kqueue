/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>

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
        int _error;
    } variants;

    variant current;

public:
    response ();
    void setup (const events &registered);
    void setup (int watch_id, int error);

    variant current_variant () const;
    events registered () const;
    int added_watch_id () const;
    int added_watch_error () const;
};

#endif // __RESPONSE_HH__
