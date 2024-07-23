/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2024 Serenity Cybersecurity, LLC
                     Author: Gleb Popov <arrowd@FreeBSD.org>
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

#include <unistd.h>

#include <cassert>
#include <iostream>
#include "test.hh"

test::test (const std::string &name_, journal &j)
: jc (j.allocate_channel (name_))
{
}

test::~test ()
{
}

void* test::run_ (void *ptr)
{
    assert (ptr != NULL);
    test *t = static_cast<test *>(ptr);
    for (bool direct : {false, true}) {
        t->setup ();
        t->run (direct);
        t->cleanup ();
    }
    return NULL;
}

void test::start ()
{
    pthread_create (&thread, NULL, test::run_, this);
}

void test::wait_for_end ()
{
    pthread_join (thread, NULL);
}

bool test::should (const std::string &test_name, bool exp)
{
    if (exp) {
        pass (test_name);
    } else {
        fail (test_name);
    }
    return exp;
}

void test::pass (const std::string &test_name)
{
    std::cout << ".";
    std::cout.flush ();
    jc.pass (test_name);
}

void test::fail (const std::string &test_name)
{
    std::cout << "x";
    std::cout.flush ();
    jc.fail (test_name);
}

void test::skip (const std::string &test_name)
{
    std::cout << "!";
    std::cout.flush ();
    jc.skip (test_name);
}
