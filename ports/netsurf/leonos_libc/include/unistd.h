#ifndef LEONOS_LIBC_UNISTD_H
#define LEONOS_LIBC_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
off_t lseek(int fd, off_t offset, int whence);
int close(int fd);
int access(const char *path, int mode);
int unlink(const char *path);
int rmdir(const char *path);
char *getcwd(char *buf, size_t size);
char *realpath(const char *path, char *resolved_path);
int chdir(const char *path);

#endif
