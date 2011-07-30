#ifndef __CONVERSIONS_H__
#define __CONVERSIONS_H__

#include <stdint.h>

uint32_t inotify_to_kqueue (uint32_t flags, int is_directory);
uint32_t kqueue_to_inotify (uint32_t flags, int is_directory);

#endif /*  __CONVERSIONS_H__ */
