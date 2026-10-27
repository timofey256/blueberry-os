#include <lib/string.h>

void* memset(void* dest, int value, size_t num) {
    unsigned char* ptr = dest;
    while (num--) {
        *ptr++ = (unsigned char)value;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t num) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    while (num--) {
        *d++ = *s++;
    }
    return dest;
}

size_t strlen(const char* s) {
    const char* p = s;
    while (*p)
        ++p;
    return (size_t)(p - s);
}

char* strncpy(char* dest, const char* src, size_t n) {
    if (n == 0)
        return dest;
    if (!src) {
        dest[0] = '\0';
        return dest;
    }

    size_t i = 0;
    while (i + 1 < n && src[i] != '\0') {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
    return dest;
}
