#ifndef XJ380_NATIVE_H
#define XJ380_NATIVE_H

#include <stdbool.h>

int xj380_native_run(const char *path, int argc, char **argv, bool debug_enabled);
const char *xj380_native_strerror(void);

#endif /* XJ380_NATIVE_H */
