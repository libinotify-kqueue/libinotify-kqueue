#ifndef __UTILS_H__
#define __UTILS_H__

char* path_concat (const char *dir, const char *file);

int safe_read  (int fd, void *data, size_t size);
int safe_write (int fd, const void *data, size_t size);

#endif /* __UTILS_H__ */
