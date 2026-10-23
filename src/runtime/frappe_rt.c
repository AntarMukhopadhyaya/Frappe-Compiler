//
// Created by antar on 22/10/26.
//
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { const char *ptr; int64_t len; } FrappeStr;

void *__frappe_alloc(int64_t size) {
    void *p = calloc(1, (size_t)size);
    if (!p && size > 0) { fputs("frappe: out of memory\n", stderr); exit(1); }
    return p;
}

void *__frappe_alloc_array(int64_t elem, int64_t n) {
    size_t total = 8 + (size_t)(elem * n);
    int64_t *p = (int64_t *)malloc(total);
    if (!p) { fputs("frappe: out of memory\n", stderr); exit(1); }
    p[0] = n;
    return (char *)p + 8;
}

__attribute__((noreturn))
void __frappe_panic(FrappeStr msg) {
    fwrite(msg.ptr, 1, (size_t)msg.len, stderr);
    fputc('\n', stderr);
    exit(1);
}

FrappeStr __frappe_str_concat(FrappeStr a, FrappeStr b) {
    size_t n = (size_t)(a.len + b.len);
    char *p = (char *)malloc(n);
    if (!p && n > 0) { fputs("frappe: out of memory\n", stderr); exit(1); }
    if (a.len) memcpy(p, a.ptr, (size_t)a.len);
    if (b.len) memcpy(p + a.len, b.ptr, (size_t)b.len);
    return (FrappeStr){ p, (int64_t)n };
}

int64_t __frappe_str_eq(FrappeStr a, FrappeStr b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}