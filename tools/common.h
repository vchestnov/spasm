#ifndef _SPASM_COMMON_H
#define _SPASM_COMMON_H

#include <stdio.h>
#include <argp.h>

struct input_matrix {
	char *filename;
	i64 prime;
};

extern struct argp echelonize_argp;
extern struct argp input_argp;
extern const char *argp_program_version;
extern const char *argp_program_bug_address;

spasm_triplet * load_input_matrix(struct input_matrix *in);
FILE * open_output(const char *filename);

#endif