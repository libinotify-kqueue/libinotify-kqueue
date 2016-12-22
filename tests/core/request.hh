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
    void receive (unsigned int timeout = 1000);
    void setup (const std::string &path, uint32_t mask);
    void setup (int rm_id);

    variant current_variant () const;
    activity activity_data () const;
    add_modify add_modify_data () const;
    remove remove_data () const;
};

#endif // __REQUEST_HH__
