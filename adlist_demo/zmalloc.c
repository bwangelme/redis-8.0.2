#include <stdlib.h>
#include <string.h>

// 简化的 zmalloc 实现，直接使用标准库
void *zmalloc(size_t size) {
    return malloc(size);
}

void *zcalloc(size_t size) {
    return calloc(1, size);
}

void *zrealloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

void zfree(void *ptr) {
    free(ptr);
}

char *zstrdup(const char *s) {
    return strdup(s);
}

size_t zmalloc_size(void *ptr) {
    // 简化实现，返回 0
    return 0;
}
