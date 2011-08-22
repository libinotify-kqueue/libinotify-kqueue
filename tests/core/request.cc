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

#include <cassert>
#include "log.hh"
#include "request.hh"

request::request ()
: action ("REQUEST")
{
}

void request::receive (unsigned int timeout)
{
    LOG (named() << ": Setting up to register an activity");
    current = REGISTER_ACTIVITY;
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

