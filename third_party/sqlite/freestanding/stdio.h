#ifndef HYPE_FREESTANDING_STDIO_H
#define HYPE_FREESTANDING_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct hype_std_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)

int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list args);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list args);
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int fputs(const char *text, FILE *stream);
int putc(int ch, FILE *stream);
int fputc(int ch, FILE *stream);
int putchar(int ch);
int getc(FILE *stream);
int fgetc(FILE *stream);
int fflush(FILE *stream);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
void clearerr(FILE *stream);
FILE *tmpfile(void);
int remove(const char *path);

#endif
