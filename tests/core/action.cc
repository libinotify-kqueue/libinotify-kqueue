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

#include "log.hh"
#include "action.hh"

action::action (const std::string &name_)
: name (name_)
, interrupted (false)
, waiting (false)
{
    pthread_mutex_init (&action_mutex, NULL);
    pthread_mutex_init (&cond_mutex, NULL);
    pthread_cond_init (&cond, NULL);

    init ();
}

action::~action ()
{
    pthread_mutex_destroy (&action_mutex);
    pthread_mutex_destroy (&cond_mutex);
    pthread_cond_destroy (&cond);
}

void action::init ()
{
    LOG (name << ": Initializing");
    interrupted = false;
    waiting = false;
}

bool action::wait ()
{
    pthread_mutex_lock (&action_mutex);

    if (waiting) {
        /* wake up the waiting thread and return */
        waiting = false;
        LOG (name << ": Resuming from sleep");
        pthread_mutex_unlock (&action_mutex);
        pthread_cond_signal (&cond);
    } else {
        /* sleep current thread for a subsequent wait */
        LOG (name << ": Going to sleep");
        waiting = true;
        pthread_mutex_unlock (&action_mutex);
        while (waiting) {
            pthread_mutex_lock (&cond_mutex);
            pthread_cond_wait (&cond, &cond_mutex);
            pthread_mutex_unlock (&cond_mutex);
        }
    }

    return !interrupted;
}

void action::interrupt ()
{
    LOG (name << ": Marking action interrupted");
    interrupted = true;
    wait ();
}

void action::reset ()
{
    LOG (name << ": Resetting");
    init ();
}

std::string action::named () const
{
    return name;
}

