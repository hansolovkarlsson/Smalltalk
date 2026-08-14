#include "environment.h"
#include "gc.h"

#include <stdlib.h>

typedef struct {
    const char *name;
    oop value;
} Binding;

static Binding *bindings = NULL;
static int count = 0;
static int capacity = 0;

int envLookup(const char *name, oop *outValue) {
    for (int i = 0; i < count; i++) {
        if (bindings[i].name == name) {
            *outValue = bindings[i].value;
            return 1;
        }
    }
    return 0;
}

void envSet(const char *name, oop value) {
    for (int i = 0; i < count; i++) {
        if (bindings[i].name == name) {
            bindings[i].value = value;
            return;
        }
    }
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 16;
        bindings = realloc(bindings, sizeof(Binding) * capacity);
    }
    bindings[count].name = name;
    bindings[count].value = value;
    count++;
}

void envMarkRoots(void) {
    for (int i = 0; i < count; i++) {
        gcMarkOop(bindings[i].value);
    }
}
