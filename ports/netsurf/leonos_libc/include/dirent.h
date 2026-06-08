#ifndef LEONOS_LIBC_DIRENT_H
#define LEONOS_LIBC_DIRENT_H

typedef struct leonos_dir DIR;

struct dirent {
    char d_name[256];
};

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **));
int alphasort(const struct dirent **a, const struct dirent **b);

#endif
