#ifndef LIB_STRING_H
#define LIB_STRING_H

#include <types.h>

void* memset(void* dest, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);

#endif
