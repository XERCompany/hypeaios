#ifndef HYPE_FREESTANDING_STRING_H
#define HYPE_FREESTANDING_STRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
int memcmp(const void *left, const void *right, size_t n);
void *memchr(const void *data, int ch, size_t n);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t n);
int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t n);
char *strchr(const char *text, int ch);
char *strrchr(const char *text, int ch);
char *strstr(const char *text, const char *needle);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strdup(const char *text);

#endif
