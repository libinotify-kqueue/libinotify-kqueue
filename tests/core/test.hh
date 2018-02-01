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

#ifndef __TEST_HH__
#define __TEST_HH__

#include <pthread.h>
#include "platform.hh"
#include "journal.hh"

class test {
    journal::channel &jc;
    pthread_t thread;

protected:
    static void* run_ (void *ptr);

    virtual void setup () = 0;
    virtual void run () = 0;
    virtual void cleanup () = 0;

public:
    test (const std::string &name_, journal &j);
    virtual ~test ();

    void start ();
    void wait_for_end ();

    bool should (const std::string &test_name, bool exp);
    void pass (const std::string &test_name);
    void fail (const std::string &test_name);
    void skip (const std::string &test_name);
};


#endif // __TEST_HH__
