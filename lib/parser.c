#include "../include/linux/parser.h"
#include "../include/linux/slab.h"
#include "../include/linux/errno.h"
#include "../include/linux/libc.h"

/* is_digit: Implement is digit. */
static int is_digit(char c)
{
	return c >= '0' && c <= '9';
}

/* is_hex_digit: Implement is hex digit. */
static int is_hex_digit(char c)
{
	return (c >= '0' && c <= '9') ||
	       (c >= 'a' && c <= 'f') ||
	       (c >= 'A' && c <= 'F');
}

/* hex_value: Implement hex value. */
static int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* match_one: Implement match one. */
static int match_one(char *s, char *p, substring_t args[])
{
	char *meta;
	int argc = 0;

	if (!p)
		return 1;

	while (1) {
		int len = -1;
		meta = NULL;
		for (char *scan = p; *scan; scan++) {
			if (*scan == '%') {
				meta = scan;
				break;
			}
		}
		if (!meta)
			return strcmp(p, s) == 0;

		if (strncmp(p, s, (size_t)(meta - p)))
			return 0;

		s += meta - p;
		p = meta + 1;

		if (is_digit(*p)) {
			len = 0;
			while (is_digit(*p)) {
				len = len * 10 + (*p - '0');
				p++;
			}
		} else if (*p == '%') {
			if (*s++ != '%')
				return 0;
			p++;
			continue;
		}

		if (argc >= MAX_OPT_ARGS)
			return 0;

		args[argc].from = s;
		switch (*p++) {
		case 's':
			if (strlen(s) == 0)
				return 0;
			if (len == -1 || len > (int)strlen(s))
				len = (int)strlen(s);
			args[argc].to = s + len;
			break;
		case 'd': {
			char *scan = s;
			if (*scan == '-')
				scan++;
			if (!is_digit(*scan))
				return 0;
			while (is_digit(*scan))
				scan++;
			args[argc].to = scan;
			break;
		}
		case 'u': {
			char *scan = s;
			if (!is_digit(*scan))
				return 0;
			while (is_digit(*scan))
				scan++;
			args[argc].to = scan;
			break;
		}
		case 'o': {
			char *scan = s;
			if (*scan < '0' || *scan > '7')
				return 0;
			while (*scan >= '0' && *scan <= '7')
				scan++;
			args[argc].to = scan;
			break;
		}
		case 'x': {
			char *scan = s;
			if (!is_hex_digit(*scan))
				return 0;
			while (is_hex_digit(*scan))
				scan++;
			args[argc].to = scan;
			break;
		}
		default:
			return 0;
		}
		s = args[argc].to;
		argc++;
	}
}

/* match_token: Implement match token. */
int match_token(char *s, match_table_t table, substring_t args[])
{
	struct match_token *p;

	for (p = table; !match_one(s, p->pattern, args); p++)
		;

	return p->token;
}

/* match_number: Implement match number. */
static int match_number(substring_t *s, int *result, int base)
{
	int sign = 1;
	int value = 0;
	char *p = s->from;

	if (p >= s->to)
		return -EINVAL;

	if (*p == '-') {
		sign = -1;
		p++;
	}

	if (base == 0) {
		if ((s->to - p) >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
			base = 16;
			p += 2;
		} else if (p < s->to && p[0] == '0') {
			base = 8;
			p++;
		} else {
			base = 10;
		}
	}

	if (p >= s->to)
		return -EINVAL;

	for (; p < s->to; p++) {
		int digit;
		if (base == 16) {
			digit = hex_value(*p);
		} else {
			if (!is_digit(*p))
				return -EINVAL;
			digit = *p - '0';
		}
		if (digit < 0 || digit >= base)
			return -EINVAL;
		value = value * base + digit;
	}

	*result = value * sign;
	return 0;
}

/* match_int: Implement match int. */
int match_int(substring_t *s, int *result)
{
	return match_number(s, result, 0);
}

/* match_octal: Implement match octal. */
int match_octal(substring_t *s, int *result)
{
	return match_number(s, result, 8);
}

/* match_hex: Implement match hex. */
int match_hex(substring_t *s, int *result)
{
	return match_number(s, result, 16);
}

/* match_strcpy: Implement match strcpy. */
void match_strcpy(char *to, substring_t *s)
{
	memcpy(to, s->from, (size_t)(s->to - s->from));
	to[s->to - s->from] = '\0';
}

/* match_strdup: Implement match strdup. */
char *match_strdup(substring_t *s)
{
	size_t len = (size_t)(s->to - s->from);
	char *buf = kmalloc(len + 1);
	if (!buf)
		return NULL;
	memcpy(buf, s->from, len);
	buf[len] = '\0';
	return buf;
}
