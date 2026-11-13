#ifndef TRANSPILER_H
#define TRANSPILER_H

#ifdef __cplusplus
extern "C" {
#endif


int transpileToBash(const char *code, const char *filename);
int transpileToBatch(const char *code, const char *filename);

#ifdef __cplusplus
}
#endif

#endif
