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

#ifndef __JOURNAL_HH__
#define __JOURNAL_HH__

#include <list>
#include <pthread.h>
#include "platform.hh"

class journal {
public:
    struct entry {
        std::string name;
        enum status_e {
            PASSED,
            FAILED,
            SKIPPED
        } status;

        explicit entry (const std::string &name_ = "", status_e status_ = PASSED);
    };

    class channel {
        typedef std::list<entry> entry_list;
        entry_list results;
        std::string name;

    public:
        channel (const std::string &name_);

        void pass (const std::string &test_name);
        void fail (const std::string &test_name);
        void skip (const std::string &test_name);

        void summarize (int &passed, int &failed, int &skipped) const;
    };

    journal ();
    ~journal ();
    channel& allocate_channel (const std::string &name);
    void summarize () const;

private:
    // It would be better to use shared pointers here, but I dont want to
    // introduce a dependency like Boost. Raw references are harmful anyway
    typedef std::list<channel> channel_list;
    mutable pthread_mutex_t channels_mutex;
    channel_list channels;
};

#endif // __JOURNAL_HH__
