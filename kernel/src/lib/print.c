// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <adt/list.h>
#include <debug.h>
#include <drivers/printer.h>
#include <lib/print.h>
#include <lib/stdarg.h>
#include <mm/heap.h>

const char SMALL_ALPHABET_OFFSET = 'a';
const char LARGE_ALPHABET_OFFSET = 'A';
const unsigned long HEXADECIMAL_BASE = 16;
const unsigned long DECIMAL_BASE = 10;
const int MAX_BUFFER_SIZE = 2 * sizeof(long int); // we don'tuse uint_64 or long long but maybe it should be it

int isdigit(const char c) {
    if (c >= '0' && c <= '9')
        return 1;
    return 0;
}

void fputs(const char* s) {
    while (*s != '\0') {
        printer_putchar(*s);
        s++;
    }
}

void puts(const char* s) {
    fputs(s);
    printer_putchar('\n');
}

static void printk_num_with_padding(unsigned long num, const unsigned long base, const char alphabet_offset, int width, const char padding) {
    char buffer[MAX_BUFFER_SIZE];
    int i = 0;
    do {
        unsigned long remainder = num % base;
        buffer[i++] = (remainder < 10) ? remainder + '0' : (remainder - 10) + alphabet_offset;
        num /= base;
    } while (num != 0);
    while (i < width) {
        printer_putchar(padding);
        --width;
    }
    while (i--) {
        printer_putchar(buffer[i]);
    }
}

static void printk_num(unsigned long num, const unsigned long base, const char alphabet_offset) {
    printk_num_with_padding(num, base, alphabet_offset, 0, ' ');
}

static void printk_num_with_hex_pref(unsigned long number, const char alphabet_offset) {
    fputs("0x");
    printk_num(number, HEXADECIMAL_BASE, alphabet_offset);
}

static void proces_memory_block(va_list* argp_pointer, const char** pointer_to_it) {
    ++*pointer_to_it;
    const block_header_t* header_pointer = va_arg(*argp_pointer, const block_header_t*);
    printk("Block addr: %p = [free: %s, size: %u, prev: %p, next: %p]\n", (void*)header_pointer, (header_pointer->free == FREE) ? "FREE" : "OCCUPIED", header_pointer->payload_size, (void*)header_pointer->previous, (void*)header_pointer->next);
}

static void process_function(va_list* argp_pointer, const char** pointer_to_it) {
    ++*pointer_to_it;
    const void* function = va_arg(*argp_pointer, const void*);
    const unsigned long* address = (unsigned long*)function;
    printk_num_with_hex_pref((unsigned long)address, SMALL_ALPHABET_OFFSET);
    printer_putchar('[');
    for (int i = 0; i < 4; ++i) {
        if (i > 0)
            printer_putchar(' ');
        printk_num_with_padding((unsigned long)address[i], HEXADECIMAL_BASE, SMALL_ALPHABET_OFFSET, 8, '0');
    }
    printer_putchar(']');
}

static void process_list(va_list* argp_pointer, const char** pointer_to_it) { // needs some work
    ++*pointer_to_it;
    const list_t* list = va_arg(*argp_pointer, const list_t*);
    printk_num_with_hex_pref((unsigned long)list, SMALL_ALPHABET_OFFSET);
    if (list_is_empty(list)) {
        fputs("[empty]");
    } else {
        printer_putchar('[');
        printk_num(list_get_size(list), DECIMAL_BASE, SMALL_ALPHABET_OFFSET);
        fputs(": ");
        for (link_t* link = list->head.next; link != &(list->head); link = link->next) {
            if (link != list->head.next)
                printer_putchar('-');
            printk_num_with_hex_pref((unsigned long)link, SMALL_ALPHABET_OFFSET);
        }
        printer_putchar(']');
    }
}

static void process_number(va_list* argp_pointer, const char** pointer_to_it) {
    char padding = ' ';
    int width = 0;
    unsigned long num;
    if (**pointer_to_it == '0') {
        padding = '0';
        ++*pointer_to_it;
    }
    while (**pointer_to_it >= '0' && **pointer_to_it <= '9') {
        width = width * 10 + (**pointer_to_it - '0');
        ++*pointer_to_it;
    }
    if (**pointer_to_it == 'd') {
        int numint = va_arg(*argp_pointer, int);
        if (numint < 0)
            printer_putchar('-');
        num = (numint < 0) ? (unsigned long)(-(long)numint) : (unsigned long)(numint);
    } else {
        num = va_arg(*argp_pointer, unsigned int);
    }
    char alphabet_offset = (**pointer_to_it == 'X') ? LARGE_ALPHABET_OFFSET : SMALL_ALPHABET_OFFSET;
    unsigned long base = (**pointer_to_it == 'u' || **pointer_to_it == 'd') ? DECIMAL_BASE : HEXADECIMAL_BASE;
    printk_num_with_padding(num, base, alphabet_offset, width, padding);
}

static void process_pointers(va_list* argp_pointer, const char** pointer_to_it) {
    const char next = *(*pointer_to_it + 1);
    switch (next) {
    case 'A': {
        proces_memory_block(argp_pointer, pointer_to_it);
    } break;
    case 'F': {
        process_function(argp_pointer, pointer_to_it);
    } break;
    case 'L': {
        process_list(argp_pointer, pointer_to_it);
    } break;
    default: {
        void* pointer = va_arg(*argp_pointer, void*);
        if (!pointer) {
            fputs("(nil)");
        } else {
            unsigned long num = (unsigned long)pointer;
            printk_num_with_hex_pref(num, SMALL_ALPHABET_OFFSET);
        }
    } break;
    }
}

static void process_specifiers(va_list* argp_pointer, const char** pointer_to_it) {
    if (isdigit(**pointer_to_it) || **pointer_to_it == 'd' || **pointer_to_it == 'u' || **pointer_to_it == 'X' || **pointer_to_it == 'x') {
        process_number(argp_pointer, pointer_to_it);
    } else if (**pointer_to_it == 'c') {
        char chr = va_arg(*argp_pointer, int);
        printer_putchar(chr);
    } else if (**pointer_to_it == 'p') {
        process_pointers(argp_pointer, pointer_to_it);
    } else if (**pointer_to_it == 's') {
        char* str = va_arg(*argp_pointer, char*);
        if (str == NULL)
            str = "(null)";
        fputs(str);
    } else {
        printer_putchar(**pointer_to_it);
    }
}

void printk(const char* format, ...) {
    va_list argp;
    va_start(argp, format);
    for (const char* it = format; *it != '\0'; ++it) {
        switch (*it) {
        case '%': {
            ++it;
            process_specifiers(&argp, &it);
        } break;
        default: {
            printer_putchar(*it);
        } break;
        }
    }
    va_end(argp);
}
