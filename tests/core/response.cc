/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  SPDX-License-Identifier: MIT

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

void response::setup (int watch_id, int error)
{
    LOG (named() << ": Passing back new watch id");
    current = WATCH_ID;
    variants._watch_id = watch_id;
    variants._error = error;
    // printf("Response: settup up for id %d\n", watch_id);
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

int response::added_watch_error () const
{
    assert (current == WATCH_ID);
    return variants._error;
}

