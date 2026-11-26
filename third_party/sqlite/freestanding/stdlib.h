#ifndef HYPE_FREESTANDING_STDLIB_H
#define HYPE_FREESTANDING_STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t count, size_t n);
void *realloc(void *ptr, size_t n);
void free(void *ptr);
int abs(int value);
long labs(long value);
long long llabs(long long value);
int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
double strtod(const char *text, char **end);
char *getenv(const char *name);
void abort(void);
void exit(int code);
int atexit(void (*fn)(void));
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
int system(const char *command);
int remove(const char *path);

#endif
