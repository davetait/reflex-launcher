#ifndef FILEOPS_H
#define FILEOPS_H

// Atomically move `from` onto `to`, replacing `to` if it exists. Returns 0 on success.
int replace_file(const char *from, const char *to);

#endif
