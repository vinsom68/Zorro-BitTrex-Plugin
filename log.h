#pragma once

#include <string.h>
#include <errno.h>

#define STR(x) #x
#define STRINGIFY(x) STR(x)
#define LINESTR STRINGIFY(__LINE__)

#define log_assert(x) if (!(x)) log_die("%s: assertion failed: " #x \
                                        " (" __FILE__ ", line " LINESTR \
										")", __FUNCTION__)

#define log_null(x) log_assert(x != NULL)

#define log_debug(msg, ...) log_info("%s: " msg, __FUNCTION__, ##__VA_ARGS__)

#define log_syserr(msg) log_die("%s: %s: %s", __FUNCTION__, msg, strerror(errno))

void log_die(char *msg, ...);
void log_info(char *msg, ...);
