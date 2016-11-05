/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bearssl.h"

#define DIRNAME        "test/x509"
#define CONFFILE       (DIRNAME "/alltests.txt")
#define DEFAULT_TIME   "2016-08-30T18:00:00Z"

static void *
xmalloc(size_t len)
{
	void *buf;

	if (len == 0) {
		return NULL;
	}
	buf = malloc(len);
	if (buf == NULL) {
		fprintf(stderr, "error: cannot allocate %lu byte(s)\n",
			(unsigned long)len);
		exit(EXIT_FAILURE);
	}
	return buf;
}

static void
xfree(void *buf)
{
	if (buf != NULL) {
		free(buf);
	}
}

static char *
xstrdup(const char *name)
{
	size_t n;
	char *s;

	if (name == NULL) {
		return NULL;
	}
	n = strlen(name) + 1;
	s = xmalloc(n);
	memcpy(s, name, n);
	return s;
}

typedef struct {
	char *buf;
	size_t ptr, len;
} string_builder;

static string_builder *
SB_new(void)
{
	string_builder *sb;

	sb = xmalloc(sizeof *sb);
	sb->len = 8;
	sb->buf = xmalloc(sb->len);
	sb->ptr = 0;
	return sb;
}

static void
SB_expand(string_builder *sb, size_t extra_len)
{
	size_t nlen;
	char *nbuf;

	if (extra_len < (sb->len - sb->ptr)) {
		return;
	}
	nlen = sb->len << 1;
	if (extra_len > (nlen - sb->ptr)) {
		nlen = sb->ptr + extra_len;
	}
	nbuf = xmalloc(nlen);
	memcpy(nbuf, sb->buf, sb->ptr);
	xfree(sb->buf);
	sb->buf = nbuf;
	sb->len = nlen;
}

static void
SB_append_char(string_builder *sb, int c)
{
	SB_expand(sb, 1);
	sb->buf[sb->ptr ++] = c;
}

/* unused
static void
SB_append_string(string_builder *sb, const char *s)
{
	size_t n;

	n = strlen(s);
	SB_expand(sb, n);
	memcpy(sb->buf + sb->ptr, s, n);
	sb->ptr += n;
}
*/

/* unused
static char *
SB_to_string(string_builder *sb)
{
	char *s;

	s = xmalloc(sb->ptr + 1);
	memcpy(s, sb->buf, sb->ptr);
	s[sb->ptr] = 0;
	return s;
}
*/

static char *
SB_contents(string_builder *sb)
{
	return sb->buf;
}

static size_t
SB_length(string_builder *sb)
{
	return sb->ptr;
}

static void
SB_set_length(string_builder *sb, size_t len)
{
	if (sb->ptr < len) {
		SB_expand(sb, len - sb->ptr);
		memset(sb->buf + sb->ptr, ' ', len - sb->ptr);
	}
	sb->ptr = len;
}

static void
SB_reset(string_builder *sb)
{
	SB_set_length(sb, 0);
}

static void
SB_free(string_builder *sb)
{
	xfree(sb->buf);
	xfree(sb);
}

typedef struct ht_elt_ {
	char *name;
	void *value;
	struct ht_elt_ *next;
} ht_elt;

typedef struct {
	size_t size;
	ht_elt **buckets;
	size_t num_buckets;
} HT;

static HT *
HT_new(void)
{
	HT *ht;
	size_t u;

	ht = xmalloc(sizeof *ht);
	ht->size = 0;
	ht->num_buckets = 8;
	ht->buckets = xmalloc(ht->num_buckets * sizeof(ht_elt *));
	for (u = 0; u < ht->num_buckets; u ++) {
		ht->buckets[u] = NULL;
	}
	return ht;
}

static uint32_t
hash_string(const char *name)
{
	uint32_t hc;

	hc = 0;
	while (*name) {
		int x;

		hc = (hc << 5) - hc;
		x = *(const unsigned char *)name;
		if (x >= 'A' && x <= 'Z') {
			x += 'a' - 'A';
		}
		hc += (uint32_t)x;
		name ++;
	}
	return hc;
}

static int
eqstring(const char *s1, const char *s2)
{
	while (*s1 && *s2) {
		int x1, x2;

		x1 = *(const unsigned char *)s1;
		x2 = *(const unsigned char *)s2;
		if (x1 >= 'A' && x1 <= 'Z') {
			x1 += 'a' - 'A';
		}
		if (x2 >= 'A' && x2 <= 'Z') {
			x2 += 'a' - 'A';
		}
		if (x1 != x2) {
			return 0;
		}
		s1 ++;
		s2 ++;
	}
	return !(*s1 || *s2);
}

static void
HT_expand(HT *ht)
{
	size_t n, n2, u;
	ht_elt **new_buckets;

	n = ht->num_buckets;
	n2 = n << 1;
	new_buckets = xmalloc(n2 * sizeof *new_buckets);
	for (u = 0; u < n2; u ++) {
		new_buckets[u] = NULL;
	}
	for (u = 0; u < n; u ++) {
		ht_elt *e, *f;

		f = NULL;
		for (e = ht->buckets[u]; e != NULL; e = f) {
			uint32_t hc;
			size_t v;

			hc = hash_string(e->name);
			v = (size_t)(hc & ((uint32_t)n2 - 1));
			f = e->next;
			e->next = new_buckets[v];
			new_buckets[v] = e;
		}
	}
	xfree(ht->buckets);
	ht->buckets = new_buckets;
	ht->num_buckets = n2;
}

static void *
HT_put(HT *ht, const char *name, void *value)
{
	uint32_t hc;
	size_t k;
	ht_elt *e, **prev;

	hc = hash_string(name);
	k = (size_t)(hc & ((uint32_t)ht->num_buckets - 1));
	prev = &ht->buckets[k];
	e = *prev;
	while (e != NULL) {
		if (eqstring(name, e->name)) {
			void *old_value;

			old_value = e->value;
			if (value == NULL) {
				*prev = e->next;
				xfree(e->name);
				xfree(e);
				ht->size --;
			} else {
				e->value = value;
			}
			return old_value;
		}
		prev = &e->next;
		e = *prev;
	}
	if (value != NULL) {
		e = xmalloc(sizeof *e);
		e->name = xstrdup(name);
		e->value = value;
		e->next = ht->buckets[k];
		ht->buckets[k] = e;
		ht->size ++;
		if (ht->size > ht->num_buckets) {
			HT_expand(ht);
		}
	}
	return NULL;
}

/* unused
static void *
HT_remove(HT *ht, const char *name)
{
	return HT_put(ht, name, NULL);
}
*/

static void *
HT_get(const HT *ht, const char *name)
{
	uint32_t hc;
	size_t k;
	ht_elt *e;

	hc = hash_string(name);
	k = (size_t)(hc & ((uint32_t)ht->num_buckets - 1));
	for (e = ht->buckets[k]; e != NULL; e = e->next) {
		if (eqstring(name, e->name)) {
			return e->value;
		}
	}
	return NULL;
}

static void
HT_clear(HT *ht, void (*free_value)(void *value))
{
	size_t u;

	for (u = 0; u < ht->num_buckets; u ++) {
		ht_elt *e, *f;

		f = NULL;
		for (e = ht->buckets[u]; e != NULL; e = f) {
			f = e->next;
			xfree(e->name);
			if (free_value != 0) {
				free_value(e->value);
			}
			xfree(e);
		}
		ht->buckets[u] = NULL;
	}
	ht->size = 0;
}

static void
HT_free(HT *ht, void (*free_value)(void *value))
{
	HT_clear(ht, free_value);
	xfree(ht->buckets);
	xfree(ht);
}

/* unused
static size_t
HT_size(HT *ht)
{
	return ht->size;
}
*/

static unsigned char *
read_all(FILE *f, size_t *len)
{
	unsigned char *buf;
	size_t ptr, blen;

	blen = 1024;
	buf = xmalloc(blen);
	ptr = 0;
	for (;;) {
		size_t rlen;

		if (ptr == blen) {
			unsigned char *buf2;

			blen <<= 1;
			buf2 = xmalloc(blen);
			memcpy(buf2, buf, ptr);
			xfree(buf);
			buf = buf2;
		}
		rlen = fread(buf + ptr, 1, blen - ptr, f);
		if (rlen == 0) {
			unsigned char *buf3;

			buf3 = xmalloc(ptr);
			memcpy(buf3, buf, ptr);
			xfree(buf);
			*len = ptr;
			return buf3;
		}
		ptr += rlen;
	}
}

static unsigned char *
read_file(const char *name, size_t *len)
{
	FILE *f;
	unsigned char *buf;

#ifdef DIRNAME
	char *dname;

	dname = xmalloc(strlen(DIRNAME) + strlen(name) + 2);
	sprintf(dname, "%s/%s", DIRNAME, name);
	name = dname;
#endif
	f = fopen(name, "rb");
	if (f == NULL) {
		fprintf(stderr, "could not open file '%s'\n", name);
		exit(EXIT_FAILURE);
	}
	buf = read_all(f, len);
	if (ferror(f)) {
		fprintf(stderr, "read error on file '%s'\n", name);
		exit(EXIT_FAILURE);
	}
	fclose(f);
#ifdef DIRNAME
	xfree(dname);
#endif
	return buf;
}

static int
parse_dec(const char *s, unsigned len, int *val)
{
	int acc;

	acc = 0;
	while (len -- > 0) {
		int c;

		c = *s ++;
		if (c >= '0' && c <= '9') {
			acc = (acc * 10) + (c - '0');
		} else {
			return -1;
		}
	}
	*val = acc;
	return 0;
}

static int
parse_choice(const char *s, const char *acceptable)
{
	int c;

	c = *s;
	while (*acceptable) {
		if (c == *acceptable ++) {
			return 0;
		}
	}
	return -1;
}

static int
month_length(int year, int month)
{
	static const int base_month_length[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	int x;

	x = base_month_length[month - 1];
	if (month == 2 && year % 4 == 0
		&& (year % 100 != 0 || year % 400 == 0))
	{
		x ++;
	}
	return x;
}

/*
 * Convert a time string to a days+seconds count. Returned value is 0
 * on success, -1 on error.
 */
static int
string_to_time(const char *s, uint32_t *days, uint32_t *seconds)
{
	int year, month, day, hour, minute, second;
	int day_of_year, leaps, i;

	if (parse_dec(s, 4, &year) < 0) {
		return -1;
	}
	s += 4;
	if (parse_choice(s ++, "-:/ ") < 0) {
		return -1;
	}
	if (parse_dec(s, 2, &month) < 0) {
		return -1;
	}
	s += 2;
	if (parse_choice(s ++, "-:/ ") < 0) {
		return -1;
	}
	if (parse_dec(s, 2, &day) < 0) {
		return -1;
	}
	s += 2;
	if (parse_choice(s ++, " T") < 0) {
		return -1;
	}
	if (parse_dec(s, 2, &hour) < 0) {
		return -1;
	}
	s += 2;
	if (parse_choice(s ++, "-:/ ") < 0) {
		return -1;
	}
	if (parse_dec(s, 2, &minute) < 0) {
		return -1;
	}
	s += 2;
	if (parse_choice(s ++, "-:/ ") < 0) {
		return -1;
	}
	if (parse_dec(s, 2, &second) < 0) {
		return -1;
	}
	s += 2;
	if (*s == '.') {
		while (*s && *s >= '0' && *s <= '9') {
			s ++;
		}
	}
	if (*s) {
		if (*s ++ != 'Z') {
			return -1;
		}
		if (*s) {
			return -1;
		}
	}

	if (month < 1 || month > 12) {
		return -1;
	}
	day_of_year = 0;
	for (i = 1; i < month; i ++) {
		day_of_year += month_length(year, i);
	}
	if (day < 1 || day > month_length(year, month)) {
		return -1;
	}
	day_of_year += (day - 1);
	leaps = (year + 3) / 4 - (year + 99) / 100 + (year + 399) / 400;

	if (hour > 23 || minute > 59 || second > 60) {
		return -1;
	}
	*days = (uint32_t)year * 365 + (uint32_t)leaps + day_of_year;
	*seconds = (uint32_t)hour * 3600 + minute * 60 + second;
	return 0;
}

static FILE *conf;
static int conf_delayed_char;
static long conf_linenum;
static string_builder *line_builder;
static long current_linenum;

static void
conf_init(const char *fname)
{
	conf = fopen(fname, "r");
	if (conf == NULL) {
		fprintf(stderr, "could not open file '%s'\n", fname);
		exit(EXIT_FAILURE);
	}
	conf_delayed_char = -1;
	conf_linenum = 1;
	line_builder = SB_new();
}

static void
conf_close(void)
{
	if (conf != NULL) {
		if (ferror(conf)) {
			fprintf(stderr, "read error on configuration file\n");
			exit(EXIT_FAILURE);
		}
		fclose(conf);
		conf = NULL;
	}
	if (line_builder != NULL) {
		SB_free(line_builder);
		line_builder = NULL;
	}
}

/*
 * Get next character from the config file.
 */
static int
conf_next_low(void)
{
	int x;

	x = conf_delayed_char;
	if (x >= 0) {
		conf_delayed_char = -1;
	} else {
		x = fgetc(conf);
		if (x == EOF) {
			x = -1;
		}
	}
	if (x == '\r') {
		x = fgetc(conf);
		if (x == EOF) {
			x = -1;
		}
		if (x != '\n') {
			conf_delayed_char = x;
			x = '\n';
		}
	}
	if (x == '\n') {
		conf_linenum ++;
	}
	return x;
}

static int
is_ws(int x)
{
	return x <= 32;
}

static int
is_name_char(int c)
{
	return (c >= 'A' && c <= 'Z')
		|| (c >= 'a' && c <= 'z')
		|| (c >= '0' && c <= '9')
		|| (c == '_' || c == '-' || c == '.');
}

/*
 * Read a complete line. This handles line continuation; empty lines and
 * comment lines are skipped; leading and trailing whitespace is removed.
 * Returned value is 0 (line read) or -1 (no line, EOF reached). The line
 * contents are accumulated in the line_builder.
 */
static int
conf_next_line(void)
{
	for (;;) {
		int c;
		int lcwb;

		SB_reset(line_builder);

		/*
		 * Get first non-whitespace character. This skips empty
		 * lines. Comment lines (first non-whitespace character
		 * is a semicolon) are also skipped.
		 */
		for (;;) {
			c = conf_next_low();
			if (c < 0) {
				return -1;
			}
			if (is_ws(c)) {
				continue;
			}
			if (c == ';') {
				for (;;) {
					c = conf_next_low();
					if (c < 0) {
						return -1;
					}
					if (c == '\n') {
						break;
					}
				}
				continue;
			}
			break;
		}

		/*
		 * Read up the remaining of the line. The line continuation
		 * sequence (final backslash) is detected and processed.
		 */
		current_linenum = conf_linenum;
		lcwb = (c == '\\');
		SB_append_char(line_builder, c);
		for (;;) {
			c = conf_next_low();
			if (c < 0) {
				break;
			}
			if (lcwb) {
				if (c == '\n') {
					SB_set_length(line_builder,
						SB_length(line_builder) - 1);
				}
				lcwb = 0;
				continue;
			}
			if (c == '\n') {
				break;
			} else if (c == '\\') {
				lcwb = 1;
			}
			SB_append_char(line_builder, c);
		}

		/*
		 * Remove trailing whitespace (if any).
		 */
		for (;;) {
			size_t u;

			u = SB_length(line_builder);
			if (u == 0 || !is_ws(
				SB_contents(line_builder)[u - 1]))
			{
				break;
			}
			SB_set_length(line_builder, u - 1);
		}

		/*
		 * We might end up with a totally empty line (in case there
		 * was a line continuation but nothing else), in which case
		 * we must loop.
		 */
		if (SB_length(line_builder) > 0) {
			return 0;
		}
	}
}

/*
 * Test whether the current line is a section header. If yes, then the
 * header name is extracted, and returned as a newly allocated string.
 * Otherwise, NULL is returned.
 */
static char *
parse_header_name(void)
{
	char *buf, *name;
	size_t u, v, w, len;

	buf = SB_contents(line_builder);
	len = SB_length(line_builder);
	if (len < 2 || buf[0] != '[' || buf[len - 1] != ']') {
		return NULL;
	}
	u = 1;
	v = len - 1;
	while (u < v && is_ws(buf[u])) {
		u ++;
	}
	while (u < v && is_ws(buf[v - 1])) {
		v --;
	}
	if (u == v) {
		return NULL;
	}
	for (w = u; w < v; w ++) {
		if (!is_name_char(buf[w])) {
			return NULL;
		}
	}
	len = v - u;
	name = xmalloc(len + 1);
	memcpy(name, buf + u, len);
	name[len] = 0;
	return name;
}

/*
 * Parse the current line as a 'name = value' pair. The pair is pushed into
 * the provided hash table. On error (including a duplicate key name),
 * this function returns -1; otherwise, it returns 0.
 */
static int
parse_keyvalue(HT *d)
{
	char *buf, *name, *value;
	size_t u, len;

	buf = SB_contents(line_builder);
	len = SB_length(line_builder);
	for (u = 0; u < len; u ++) {
		if (!is_name_char(buf[u])) {
			break;
		}
	}
	if (u == 0) {
		return -1;
	}
	name = xmalloc(u + 1);
	memcpy(name, buf, u);
	name[u] = 0;
	if (HT_get(d, name) != NULL) {
		xfree(name);
		return -1;
	}
	while (u < len && is_ws(buf[u])) {
		u ++;
	}
	if (u >= len || buf[u] != '=') {
		xfree(name);
		return -1;
	}
	u ++;
	while (u < len && is_ws(buf[u])) {
		u ++;
	}
	value = xmalloc(len - u + 1);
	memcpy(value, buf + u, len - u);
	value[len - u] = 0;
	HT_put(d, name, value);
	xfree(name);
	return 0;
}

/*
 * Public keys, indexed by name. Elements are pointers to br_x509_pkey
 * structures.
 */
static HT *keys;

/*
 * Trust anchors, indexed by name. Elements are pointers to
 * test_trust_anchor structures.
 */
static HT *trust_anchors;

typedef struct {
	unsigned char *dn;
	size_t dn_len;
	unsigned flags;
	char *key_name;
} test_trust_anchor;

/*
 * Test case: trust anchors, certificates (file names), key type and
 * usage, expected status and EE public key.
 */
typedef struct {
	char *name;
	char **ta_names;
	char **cert_names;
	char *servername;
	unsigned key_type_usage;
	unsigned status;
	char *ee_key_name;
	unsigned hashes;
	uint32_t days, seconds;
} test_case;

static test_case *all_chains;
static size_t all_chains_ptr, all_chains_len;

static void
free_key(void *value)
{
	br_x509_pkey *pk;

	pk = value;
	switch (pk->key_type) {
	case BR_KEYTYPE_RSA:
		xfree((void *)pk->key.rsa.n);
		xfree((void *)pk->key.rsa.e);
		break;
	case BR_KEYTYPE_EC:
		xfree((void *)pk->key.ec.q);
		break;
	default:
		fprintf(stderr, "unknown key type: %d\n", pk->key_type);
		exit(EXIT_FAILURE);
		break;
	}
	xfree(pk);
}

static void
free_trust_anchor(void *value)
{
	test_trust_anchor *ttc;

	ttc = value;
	xfree(ttc->dn);
	xfree(ttc->key_name);
	xfree(ttc);
}

static void
free_test_case_contents(test_case *tc)
{
	size_t u;

	xfree(tc->name);
	for (u = 0; tc->ta_names[u]; u ++) {
		xfree(tc->ta_names[u]);
	}
	xfree(tc->ta_names);
	for (u = 0; tc->cert_names[u]; u ++) {
		xfree(tc->cert_names[u]);
	}
	xfree(tc->cert_names);
	xfree(tc->servername);
	xfree(tc->ee_key_name);
}

static char *
get_value(char *objtype, HT *objdata, long linenum, char *name)
{
	char *value;

	value = HT_get(objdata, name);
	if (value == NULL) {
		fprintf(stderr,
			"missing property '%s' in section '%s' (line %ld)\n",
			name, objtype, linenum);
		exit(EXIT_FAILURE);
	}
	return value;
}

static unsigned char *
parse_hex(const char *name, long linenum, const char *value, size_t *len)
{
	unsigned char *buf;

	buf = NULL;
	for (;;) {
		size_t u, ptr;
		int acc, z;

		ptr = 0;
		acc = 0;
		z = 0;
		for (u = 0; value[u]; u ++) {
			int c;

			c = value[u];
			if (c >= '0' && c <= '9') {
				c -= '0';
			} else if (c >= 'A' && c <= 'F') {
				c -= 'A' - 10;
			} else if (c >= 'a' && c <= 'f') {
				c -= 'a' - 10;
			} else if (c == ' ' || c == ':') {
				continue;
			} else {
				fprintf(stderr, "invalid hexadecimal character"
					" in '%s' (line %ld)\n",
					name, linenum);
				exit(EXIT_FAILURE);
			}
			if (z) {
				if (buf != NULL) {
					buf[ptr] = (acc << 4) + c;
				}
				ptr ++;
			} else {
				acc = c;
			}
			z = !z;
		}
		if (z) {
			fprintf(stderr, "invalid hexadecimal value (partial"
				" byte) in '%s' (line %ld)\n",
				name, linenum);
			exit(EXIT_FAILURE);
		}
		if (buf == NULL) {
			buf = xmalloc(ptr);
		} else {
			*len = ptr;
			return buf;
		}
	}
}

static char **
split_names(const char *value)
{
	char **names;
	size_t len;

	names = NULL;
	len = strlen(value);
	for (;;) {
		size_t u, ptr;

		ptr = 0;
		u = 0;
		while (u < len) {
			size_t v;

			while (u < len && is_ws(value[u])) {
				u ++;
			}
			v = u;
			while (v < len && !is_ws(value[v])) {
				v ++;
			}
			if (v > u) {
				if (names != NULL) {
					char *name;

					name = xmalloc(v - u + 1);
					memcpy(name, value + u, v - u);
					name[v - u] = 0;
					names[ptr] = name;
				}
				ptr ++;
			}
			u = v;
		}
		if (names == NULL) {
			names = xmalloc((ptr + 1) * sizeof *names);
		} else {
			names[ptr] = NULL;
			return names;
		}
	}
}

static int
string_to_hash(const char *name)
{
	char tmp[20];
	size_t u, v;

	for (u = 0, v = 0; name[u]; u ++) {
		int c;

		c = name[u];
		if ((c >= '0' && c <= '9')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z'))
		{
			tmp[v ++] = c;
			if (v == sizeof tmp) {
				return -1;
			}
		}
	}
	tmp[v] = 0;
	if (eqstring(tmp, "md5")) {
		return br_md5_ID;
	} else if (eqstring(tmp, "sha1")) {
		return br_sha1_ID;
	} else if (eqstring(tmp, "sha224")) {
		return br_sha224_ID;
	} else if (eqstring(tmp, "sha256")) {
		return br_sha256_ID;
	} else if (eqstring(tmp, "sha384")) {
		return br_sha384_ID;
	} else if (eqstring(tmp, "sha512")) {
		return br_sha512_ID;
	} else {
		return -1;
	}
}

static int
string_to_curve(const char *name)
{
	char tmp[20];
	size_t u, v;

	for (u = 0, v = 0; name[u]; u ++) {
		int c;

		c = name[u];
		if ((c >= '0' && c <= '9')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z'))
		{
			tmp[v ++] = c;
			if (v == sizeof tmp) {
				return -1;
			}
		}
	}
	tmp[v] = 0;
	if (eqstring(tmp, "p256") || eqstring(tmp, "secp256r1")) {
		return BR_EC_secp256r1;
	} else if (eqstring(tmp, "p384") || eqstring(tmp, "secp384r1")) {
		return BR_EC_secp384r1;
	} else if (eqstring(tmp, "p521") || eqstring(tmp, "secp521r1")) {
		return BR_EC_secp521r1;
	} else {
		return -1;
	}
}

static void
parse_object(char *objtype, HT *objdata, long linenum)
{
	char *name;

	name = get_value(objtype, objdata, linenum, "name");
	if (eqstring(objtype, "key")) {
		char *stype;
		br_x509_pkey *pk;

		stype = get_value(objtype, objdata, linenum, "type");
		pk = xmalloc(sizeof *pk);
		if (eqstring(stype, "RSA")) {
			char *sn, *se;

			sn = get_value(objtype, objdata, linenum, "n");
			se = get_value(objtype, objdata, linenum, "e");
			pk->key_type = BR_KEYTYPE_RSA;
			pk->key.rsa.n = parse_hex("modulus", linenum,
				sn, &pk->key.rsa.nlen);
			pk->key.rsa.e = parse_hex("exponent", linenum,
				se, &pk->key.rsa.elen);
		} else if (eqstring(stype, "EC")) {
			char *sc, *sq;
			int curve;

			sc = get_value(objtype, objdata, linenum, "curve");
			sq = get_value(objtype, objdata, linenum, "q");
			curve = string_to_curve(sc);
			if (curve < 0) {
				fprintf(stderr, "unknown curve name: '%s'"
					" (line %ld)\n", sc, linenum);
				exit(EXIT_FAILURE);
			}
			pk->key_type = BR_KEYTYPE_EC;
			pk->key.ec.curve = curve;
			pk->key.ec.q = parse_hex("public point", linenum,
				sq, &pk->key.ec.qlen);
		} else {
			fprintf(stderr, "unknown key type '%s' (line %ld)\n",
				stype, linenum);
			exit(EXIT_FAILURE);
		}
		if (HT_put(keys, name, pk) != NULL) {
			fprintf(stderr, "duplicate key: '%s' (line %ld)\n",
				name, linenum);
			exit(EXIT_FAILURE);
		}
	} else if (eqstring(objtype, "anchor")) {
		char *dnfile, *kname, *tatype;
		test_trust_anchor *tta;

		dnfile = get_value(objtype, objdata, linenum, "DN_file");
		kname = get_value(objtype, objdata, linenum, "key");
		tatype = get_value(objtype, objdata, linenum, "type");
		tta = xmalloc(sizeof *tta);
		tta->dn = read_file(dnfile, &tta->dn_len);
		tta->key_name = xstrdup(kname);
		if (eqstring(tatype, "CA")) {
			tta->flags = BR_X509_TA_CA;
		} else if (eqstring(tatype, "EE")) {
			tta->flags = 0;
		} else {
			fprintf(stderr,
				"unknown trust anchor type: '%s' (line %ld)\n",
				tatype, linenum);
		}
		if (HT_put(trust_anchors, name, tta) != NULL) {
			fprintf(stderr,
				"duplicate trust anchor: '%s' (line %ld)\n",
				name, linenum);
			exit(EXIT_FAILURE);
		}
	} else if (eqstring(objtype, "chain")) {
		test_case tc;
		char *ktype, *kusage, *sstatus, *shashes, *stime;

		ktype = get_value(objtype, objdata, linenum, "keytype");
		kusage = get_value(objtype, objdata, linenum, "keyusage");
		sstatus = get_value(objtype, objdata, linenum, "status");
		tc.name = xstrdup(name);
		tc.ta_names = split_names(
			get_value(objtype, objdata, linenum, "anchors"));
		tc.cert_names = split_names(
			get_value(objtype, objdata, linenum, "chain"));
		tc.servername = xstrdup(HT_get(objdata, "servername"));
		if (eqstring(ktype, "RSA")) {
			tc.key_type_usage = BR_KEYTYPE_RSA;
		} else if (eqstring(ktype, "EC")) {
			tc.key_type_usage = BR_KEYTYPE_EC;
		} else {
			fprintf(stderr,
				"unknown key type: '%s' (line %ld)\n",
				ktype, linenum);
			exit(EXIT_FAILURE);
		}
		if (eqstring(kusage, "KEYX")) {
			tc.key_type_usage |= BR_KEYTYPE_KEYX;
		} else if (eqstring(kusage, "SIGN")) {
			tc.key_type_usage |= BR_KEYTYPE_SIGN;
		} else {
			fprintf(stderr,
				"unknown key usage: '%s' (line %ld)\n",
				kusage, linenum);
			exit(EXIT_FAILURE);
		}
		tc.status = (unsigned)atoi(sstatus);
		if (tc.status == 0) {
			tc.ee_key_name = xstrdup(
				get_value(objtype, objdata, linenum, "eekey"));
		} else {
			tc.ee_key_name = NULL;
		}
		shashes = HT_get(objdata, "hashes");
		if (shashes == NULL) {
			tc.hashes = (unsigned)-1;
		} else {
			char **hns;
			size_t u;

			tc.hashes = 0;
			hns = split_names(shashes);
			for (u = 0;; u ++) {
				char *hn;
				int id;

				hn = hns[u];
				if (hn == NULL) {
					break;
				}
				id = string_to_hash(hn);
				if (id < 0) {
					fprintf(stderr,
						"unknown hash function '%s'"
						" (line %ld)\n", hn, linenum);
					exit(EXIT_FAILURE);
				}
				tc.hashes |= (unsigned)1 << id;
				xfree(hn);
			}
			xfree(hns);
		}
		stime = HT_get(objdata, "time");
		if (stime == NULL) {
			stime = DEFAULT_TIME;
		}
		if (string_to_time(stime, &tc.days, &tc.seconds) < 0) {
			fprintf(stderr, "invalid time string '%s' (line %ld)\n",
				stime, linenum);
			exit(EXIT_FAILURE);
		}
		if (all_chains_ptr == all_chains_len) {
			if (all_chains_len == 0) {
				all_chains_len = 8;
				all_chains = xmalloc(
					all_chains_len * sizeof *all_chains);
			} else {
				test_case *ntc;
				size_t nlen;

				nlen = all_chains_len << 1;
				ntc = xmalloc(nlen * sizeof *ntc);
				memcpy(ntc, all_chains,
					all_chains_len * sizeof *all_chains);
				xfree(all_chains);
				all_chains = ntc;
				all_chains_len = nlen;
			}
		}
		all_chains[all_chains_ptr ++] = tc;
	} else {
		fprintf(stderr, "unknown section type '%s' (line %ld)\n",
			objtype, linenum);
		exit(EXIT_FAILURE);
	}
}

static void
process_conf_file(const char *fname)
{
	char *objtype;
	HT *objdata;
	long objlinenum;

	keys = HT_new();
	trust_anchors = HT_new();
	all_chains = NULL;
	all_chains_ptr = 0;
	all_chains_len = 0;
	conf_init(fname);
	objtype = NULL;
	objdata = HT_new();
	objlinenum = 0;
	for (;;) {
		char *hname;

		if (conf_next_line() < 0) {
			break;
		}
		hname = parse_header_name();
		if (hname != NULL) {
			if (objtype != NULL) {
				parse_object(objtype, objdata, objlinenum);
				HT_clear(objdata, xfree);
				xfree(objtype);
			}
			objtype = hname;
			objlinenum = current_linenum;
			continue;
		}
		if (objtype == NULL) {
			fprintf(stderr, "no current section (line %ld)\n",
				current_linenum);
			exit(EXIT_FAILURE);
		}
		if (parse_keyvalue(objdata) < 0) {
			fprintf(stderr, "wrong configuration, line %ld\n",
				current_linenum);
			exit(EXIT_FAILURE);
		}
	}
	if (objtype != NULL) {
		parse_object(objtype, objdata, objlinenum);
		xfree(objtype);
	}
	HT_free(objdata, xfree);
	conf_close();
}

static const struct {
	int id;
	const br_hash_class *impl;
} hash_impls[] = {
	{ br_md5_ID, &br_md5_vtable },
	{ br_sha1_ID, &br_sha1_vtable },
	{ br_sha224_ID, &br_sha224_vtable },
	{ br_sha256_ID, &br_sha256_vtable },
	{ br_sha384_ID, &br_sha384_vtable },
	{ br_sha512_ID, &br_sha512_vtable },
	{ 0, NULL }
};

typedef struct {
	unsigned char *data;
	size_t len;
} blob;

static int
eqbigint(const unsigned char *b1, size_t b1_len,
	const unsigned char *b2, size_t b2_len)
{
	while (b1_len > 0 && *b1 == 0) {
		b1 ++;
		b1_len --;
	}
	while (b2_len > 0 && *b2 == 0) {
		b2 ++;
		b2_len --;
	}
	return b1_len == b2_len && memcmp(b1, b2, b1_len) == 0;
}

static int
eqpkey(const br_x509_pkey *pk1, const br_x509_pkey *pk2)
{
	if (pk1 == pk2) {
		return 1;
	}
	if (pk1 == NULL || pk2 == NULL) {
		return 0;
	}
	if (pk1->key_type != pk2->key_type) {
		return 0;
	}
	switch (pk1->key_type) {
	case BR_KEYTYPE_RSA:
		return eqbigint(pk1->key.rsa.n, pk1->key.rsa.nlen,
			pk2->key.rsa.n, pk2->key.rsa.nlen)
			&& eqbigint(pk1->key.rsa.e, pk1->key.rsa.elen,
			pk2->key.rsa.e, pk2->key.rsa.elen);
	case BR_KEYTYPE_EC:
		return pk1->key.ec.curve == pk2->key.ec.curve
			&& pk1->key.ec.qlen == pk2->key.ec.qlen
			&& memcmp(pk1->key.ec.q,
				pk2->key.ec.q, pk1->key.ec.qlen) == 0;
	default:
		fprintf(stderr, "unknown key type: %d\n", pk1->key_type);
		exit(EXIT_FAILURE);
		break;
	}
	return 0;
}

static size_t max_dp_usage;
static size_t max_rp_usage;

static void
run_test_case(test_case *tc)
{
	br_x509_minimal_context ctx;
	br_x509_trust_anchor *anchors;
	size_t num_anchors;
	size_t u;
	const br_hash_class *dnhash;
	size_t num_certs;
	blob *certs;
	br_x509_pkey *ee_pkey_ref;
	const br_x509_pkey *ee_pkey;
	unsigned status;

	printf("%s: ", tc->name);
	fflush(stdout);

	/*
	 * Get the hash function to use for hashing DN. We can use just
	 * any supported hash function, but for the elegance of things,
	 * we will use one of the hash function implementations
	 * supported for this test case (with SHA-1 as fallback).
	 */
	dnhash = &br_sha1_vtable;
	for (u = 0; hash_impls[u].id; u ++) {
		if ((tc->hashes & ((unsigned)1 << (hash_impls[u].id))) != 0) {
			dnhash = hash_impls[u].impl;
		}
	}

	/*
	 * Get trust anchors.
	 */
	for (num_anchors = 0; tc->ta_names[num_anchors]; num_anchors ++);
	anchors = xmalloc(num_anchors * sizeof *anchors);
	for (u = 0; tc->ta_names[u]; u ++) {
		test_trust_anchor *tta;
		br_x509_pkey *tak;

		tta = HT_get(trust_anchors, tc->ta_names[u]);
		if (tta == NULL) {
			fprintf(stderr, "no such trust anchor: '%s'\n",
				tc->ta_names[u]);
			exit(EXIT_FAILURE);
		}
		tak = HT_get(keys, tta->key_name);
		if (tak == NULL) {
			fprintf(stderr, "no such public key: '%s'\n",
				tta->key_name);
			exit(EXIT_FAILURE);
		}
		anchors[u].dn = tta->dn;
		anchors[u].dn_len = tta->dn_len;
		anchors[u].flags = tta->flags;
		anchors[u].pkey = *tak;
	}

	/*
	 * Read all relevant certificates.
	 */
	for (num_certs = 0; tc->cert_names[num_certs]; num_certs ++);
	certs = xmalloc(num_certs * sizeof *certs);
	for (u = 0; u < num_certs; u ++) {
		certs[u].data = read_file(tc->cert_names[u], &certs[u].len);
	}

	/*
	 * Get expected EE public key (if any).
	 */
	if (tc->ee_key_name == NULL) {
		ee_pkey_ref = NULL;
	} else {
		ee_pkey_ref = HT_get(keys, tc->ee_key_name);
		if (ee_pkey_ref == NULL) {
			fprintf(stderr, "no such public key: '%s'\n",
				tc->ee_key_name);
			exit(EXIT_FAILURE);
		}
	}

	/*
	 * Initialise the engine.
	 */
	br_x509_minimal_init(&ctx, dnhash, anchors, num_anchors);
	for (u = 0; hash_impls[u].id; u ++) {
		int id;

		id = hash_impls[u].id;
		if ((tc->hashes & ((unsigned)1 << id)) != 0) {
			br_x509_minimal_set_hash(&ctx, id, hash_impls[u].impl);
		}
	}
	br_x509_minimal_set_rsa(&ctx, br_rsa_i32_pkcs1_vrfy);
	br_x509_minimal_set_ecdsa(&ctx,
		&br_ec_prime_i31, br_ecdsa_i31_vrfy_asn1);

	/*
	 * Set the validation date.
	 */
	br_x509_minimal_set_time(&ctx, tc->days, tc->seconds);

	/*
	 * Put "canaries" to detect actual stack usage.
	 */
	for (u = 0; u < (sizeof ctx.dp_stack) / sizeof(uint32_t); u ++) {
		ctx.dp_stack[u] = 0xA7C083FE;
	}
	for (u = 0; u < (sizeof ctx.rp_stack) / sizeof(uint32_t); u ++) {
		ctx.rp_stack[u] = 0xA7C083FE;
	}

	/*
	 * Run the engine. We inject certificates by chunks of 100 bytes
	 * in order to exercise the coroutine API.
	 */
	ctx.vtable->start_chain(&ctx.vtable,
		tc->key_type_usage, tc->servername);
	for (u = 0; u < num_certs; u ++) {
		size_t v;

		ctx.vtable->start_cert(&ctx.vtable, certs[u].len);
		v = 0;
		while (v < certs[u].len) {
			size_t w;

			w = certs[u].len - v;
			if (w > 100) {
				w = 100;
			}
			ctx.vtable->append(&ctx.vtable, certs[u].data + v, w);
			v += w;
		}
		ctx.vtable->end_cert(&ctx.vtable);
	}
	status = ctx.vtable->end_chain(&ctx.vtable);
	ee_pkey = ctx.vtable->get_pkey(&ctx.vtable);

	/*
	 * Check results. Note that we may still get a public key if
	 * the path is "not trusted" (but otherwise fine).
	 */
	if (status != tc->status) {
		fprintf(stderr, "wrong status (got %d, expected %d)\n",
			status, tc->status);
		exit(EXIT_FAILURE);
	}
	if (status == BR_ERR_X509_NOT_TRUSTED) {
		ee_pkey = NULL;
	}
	if (!eqpkey(ee_pkey, ee_pkey_ref)) {
		fprintf(stderr, "wrong EE public key\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * Check stack usage.
	 */
	for (u = (sizeof ctx.dp_stack) / sizeof(uint32_t); u > 0; u --) {
		if (ctx.dp_stack[u - 1] != 0xA7C083FE) {
			if (max_dp_usage < u) {
				max_dp_usage = u;
			}
			break;
		}
	}
	for (u = (sizeof ctx.rp_stack) / sizeof(uint32_t); u > 0; u --) {
		if (ctx.rp_stack[u - 1] != 0xA7C083FE) {
			if (max_rp_usage < u) {
				max_rp_usage = u;
			}
			break;
		}
	}

	/*
	 * Release everything.
	 */
	for (u = 0; u < num_certs; u ++) {
		xfree(certs[u].data);
	}
	xfree(certs);
	xfree(anchors);
	printf("OK\n");
}

int
main(void)
{
	size_t u;

	process_conf_file(CONFFILE);

	max_dp_usage = 0;
	max_rp_usage = 0;
	for (u = 0; u < all_chains_ptr; u ++) {
		run_test_case(&all_chains[u]);
	}

	printf("Maximum data stack usage:    %u\n", (unsigned)max_dp_usage);
	printf("Maximum return stack usage:  %u\n", (unsigned)max_rp_usage);

	HT_free(keys, free_key);
	HT_free(trust_anchors, free_trust_anchor);
	for (u = 0; u < all_chains_ptr; u ++) {
		free_test_case_contents(&all_chains[u]);
	}
	xfree(all_chains);
	return 0;
}
