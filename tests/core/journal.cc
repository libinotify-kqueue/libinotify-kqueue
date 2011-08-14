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

void journal::channel::summarize (int &passed, int &failed) const
{
    passed = 0;
    failed = 0;

    bool header_printed = false;

    for (entry_list::const_iterator iter = results.begin();
         iter != results.end();
         ++iter) {

        if (iter->status == entry::PASSED) {
            ++passed;
        } else {
            ++failed;

            if (!header_printed) {
                header_printed = true;
                std::cout << std::endl << "In test \"" << name << "\":" << std::endl;
            }
            std::cout << "    failed: " << iter->name << std::endl;
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

    int total_passed = 0, total_failed = 0;

    std::cout << std::endl;

    for (channel_list::const_iterator iter = channels.begin();
         iter != channels.end();
         ++iter) {
        int passed = 0, failed = 0;
        iter->summarize (passed, failed);
        total_passed += passed;
        total_failed += failed;
    }

    int total = total_passed + total_failed;
    if (total_failed) {
        std::cout << std::endl;
    }
    std::cout << "--------------------" << std::endl
              << "     Run: " << total << std::endl
              << "  Passed: " << total_passed << std::endl
              << "  Failed: " << total_failed << std::endl;

    pthread_mutex_unlock (&channels_mutex);
}
