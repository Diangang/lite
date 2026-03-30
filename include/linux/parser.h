#ifndef LINUX_PARSER_H
#define LINUX_PARSER_H

struct match_token {
	int token;
	char *pattern;
};

typedef struct match_token match_table_t[];

enum { MAX_OPT_ARGS = 3 };

typedef struct {
	char *from;
	char *to;
} substring_t;

int match_token(char *s, match_table_t table, substring_t args[]);
int match_int(substring_t *s, int *result);
int match_octal(substring_t *s, int *result);
int match_hex(substring_t *s, int *result);
void match_strcpy(char *to, substring_t *s);
char *match_strdup(substring_t *s);

#endif
