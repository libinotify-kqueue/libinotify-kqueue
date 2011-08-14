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
            FAILED
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

        void summarize (int &passed, int &failed) const;
    };

    journal ();
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
