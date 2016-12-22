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

#include <iostream>
#include "journal.hh"

journal::entry::entry (const std::string &name_, status_e status_)
: name (name_)
, status (status_)
{
}

journal::channel::channel (const std::string &name_)
: name (name_)
{
}

void journal::channel::pass (const std::string &test_name)
{
    results.push_back (entry (test_name, entry::PASSED));
}

void journal::channel::fail (const std::string &test_name)
{
    results.push_back (entry (test_name, entry::FAILED));
}

void journal::channel::skip (const std::string &test_name)
{
    results.push_back (entry (test_name, entry::SKIPPED));
}

void journal::channel::summarize (int &passed, int &failed, int &skipped) const
{
    passed = 0;
    failed = 0;
    skipped = 0;

    bool header_printed = false;

    for (entry_list::const_iterator iter = results.begin();
         iter != results.end();
         ++iter) {

        if (iter->status == entry::PASSED) {
            ++passed;
        } else {
            if (!header_printed) {
                header_printed = true;
                std::cout << std::endl << "In test \"" << name << "\":" << std::endl;
            }
            if (iter->status == entry::FAILED) {
                ++failed;
                std::cout << "    failed: " << iter->name << std::endl;
            } else {
                ++skipped;
                std::cout << "   skipped: " << iter->name << std::endl;
            }
        }
    }

    // if (header_printed) {
    //     std::cout << std::endl;
    // }
}

journal::journal ()
{
    pthread_mutex_init (&channels_mutex, NULL);
}

journal::~journal ()
{
    pthread_mutex_destroy (&channels_mutex);
}

journal::channel& journal::allocate_channel (const std::string &name)
{
    pthread_mutex_lock (&channels_mutex);
    channels.push_back (journal::channel (name));
    journal::channel &ref = *channels.rbegin();
    pthread_mutex_unlock (&channels_mutex);
    return ref;
}

void journal::summarize () const
{
    pthread_mutex_lock (&channels_mutex);

    int total_passed = 0, total_failed = 0, total_skipped = 0;

    std::cout << std::endl;

    for (channel_list::const_iterator iter = channels.begin();
         iter != channels.end();
         ++iter) {
        int passed = 0, failed = 0, skipped = 0;
        iter->summarize (passed, failed, skipped);
        total_passed += passed;
        total_failed += failed;
        total_skipped += skipped;
    }

    int total = total_passed + total_failed + total_skipped;
    if (total_failed || total_skipped) {
        std::cout << std::endl;
    }
    std::cout << "--------------------" << std::endl
              << "     Run: " << total << std::endl
              << "  Passed: " << total_passed << std::endl
              << "  Failed: " << total_failed << std::endl
              << " Skipped: " << total_skipped << std::endl;

    pthread_mutex_unlock (&channels_mutex);
}
