#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser/parser.h"
#include "transpiler/transpiler.h"

static const struct {
    const char *name;
    int (*transpile)(const char *code, const char *filename);
} targets[] = {
    {"bash", transpileToBash},
    {"batch", transpileToBatch},
};

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *code = malloc((size_t)size + 1);
    if (code == NULL) {
        fclose(f);
        return NULL;
    }

    size_t len = fread(code, 1, size, f);
    code[len] = '\0';

    fclose(f);
    return code;
}

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s <arquivo> [bash|batch]\n", argv[0]);
        return 1;
    }

    const char *name = argc == 3 ? argv[2] : "bash";
    size_t i = 0;
    size_t n = sizeof(targets) / sizeof(targets[0]);

    while (i < n && strcmp(targets[i].name, name) != 0)
        i++;

    if (i == n) {
        fprintf(stderr, "Unknown target '%s', use bash or batch\n", name);
        return 1;
    }

    char *code = read_file(argv[1]);
    if (code == NULL) {
        perror(argv[1]);
        return 1;
    }

    int err = Parse(code) || targets[i].transpile(code, argv[1]);
    free(code);

    if (err)
        return 1;

    return 0; // DO NOT TOUCH THIS!!!
}
