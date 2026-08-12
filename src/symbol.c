#include "symbol.h"

#include <stdlib.h>
#include <string.h>

static char **table = NULL;
static int count = 0;
static int capacity = 0;

static char *dupString(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

const char *intern(const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(table[i], name) == 0) {
            return table[i];
        }
    }
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 32;
        table = realloc(table, sizeof(char *) * capacity);
    }
    table[count] = dupString(name);
    return table[count++];
}
