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

#ifndef __CONSUMER_HH__
#define __CONSUMER_HH__

#include "platform.hh"
#include "request.hh"
#include "response.hh"
#include "inotify_client.hh"

class consumer {
    std::string path;
    inotify_client ino;
    pthread_t self;

    // yes, these methods take arguments by a value. intentionally
    void register_activity (request::activity activity);
    void add_modify_watch (request::add_modify add_modify);
    void remove_watch (request::remove remove);

public:
    request input;
    response output;

    consumer ();
    ~consumer ();
    static void* run_ (void *ptr);
    void run ();
    int get_fd ();
};


#endif // __CONSUMER_HH__
