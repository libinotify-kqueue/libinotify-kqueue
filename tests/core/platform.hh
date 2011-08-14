#ifndef __PLATFORM_HH__
#define __PLATFORM_HH__

#include <cstddef> // NULL
#include <string>

#ifdef __linux__
#  include <sys/inotify.h>
#  include <cstdint> // uint32_t, requires -std=c++0x
#elif defined (__NetBSD__)
#  include "inotify.h"
#  include <stdint.h>
#else
#  error Currently unsupported
#endif

#endif //  __PLATFORM_HH__
