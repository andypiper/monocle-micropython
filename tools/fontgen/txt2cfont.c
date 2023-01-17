#include <stddef.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

typedef struct glyph glyph_t;

enum config {
	GLYPH_MAX_SIZE = 32*16,
	ASCII_FIRST = ' ',
};

struct glyph {
	char c;
	size_t width, height;
	uint8_t buf[GLYPH_MAX_SIZE / 8];
};

struct {
	size_t lineno;
	FILE *fp;
	size_t sz;
	char *line;
	int unget;
	char ascii;
} ctx;

static char *flag_t = "uint8_t const";
static char *flag_a;
static char *flag_p = "";

static void
fatal(char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	if (ctx.lineno > 0)
		fprintf(stderr, "line %zd: ", ctx.lineno);
	vfprintf(stderr, fmt, va);
	fputc('\n', stderr);
	exit(1);
}

static int
parse_name(char *s)
{
	if (ctx.ascii > '~')
		fatal("more characters than print_able ascii");
	if (s[0] == '\0' || s[1] != ':')
		fatal("expected '%c:' (with '%c' any char)", '%', '%');
	if (s[0] != ctx.ascii)
		fatal("expected '%c' got '%c'", ctx.ascii, s[0]);
	return s[0];
}

static void
print_glyph(glyph_t *g)
{
	size_t sz;

	/* +7 to include the last incomplete byte */
	sz = (g->height * g->width + 7) / 8;

	printf("\t/* %c */ %zd,", g->c, g->width);
	for (size_t i = 0; i < sz; i++)
		printf(" 0x%02"PRIX8",", g->buf[i]);
	printf("\n");
}

static void
add_bit(uint8_t *buf, size_t sz, size_t *byte, size_t *bit, int val)
{
	if (*byte >= sz)
		fatal("character glyph too long,"
		  " change  at the top of %s", __FILE__);
	buf[*byte] |= val<<*bit;
	if (++*bit == 8) {
		*bit = 0;
		(*byte)++;
	}
}

static int
parse_data_line(char *s, uint8_t *buf, size_t sz, size_t *byte, size_t *bit)
{
	int width;

	width = 0;
	for (; *s != '\0'; s++) {
		switch (*s) {
		case '\0':
			break;
		case ' ':
		case '\t':
			continue;
		case '.':
		case '#':
			add_bit(buf, sz, byte, bit, *s == '#');
			width++;
			break;
		default:
			fatal("unknown character in data line: '%c'", *s);
		}
	}
	return width;
}

static char*
font_get_line(void)
{
	if (ctx.unget == 1) {
		ctx.unget = 0;
		assert(ctx.line != NULL);
		return ctx.line;
	}

	do{
		ctx.lineno++;
		if (getline(&ctx.line, &ctx.sz, ctx.fp) == -1) {
			if (ferror(ctx.fp))
				fatal("getline: %s", strerror(errno));
			return NULL;
		}
		ctx.line[strcspn(ctx.line, "\r\n")] = '\0';
	}while (ctx.line[strspn(ctx.line, " \t")] == '\0');
	return ctx.line;
}

static void
font_unget_line(void)
{
	ctx.unget = 1;
}

static int
parse_glyph(glyph_t *g)
{
	char *s;
	size_t byte, bit, w;

	s = font_get_line();
	if (s == NULL)
		return 0;

	g->c = parse_name(s);

	memset(g->buf, 0, sizeof g->buf);
	byte = bit = g->width = g->height = 0;
	while ((s = font_get_line())) {
		if (*s++ != '\t') {
			font_unget_line();
			break;
		}
		w = parse_data_line(s, g->buf, sizeof g->buf, &byte, &bit);
		if (g->width > 0 && g->width != w)
			fatal("glyph '%c' changing its width", g->c); 
		g->width = w;
		g->height++;
	}
	return 1;
}

char *
font_name(char *buf, size_t sz, char *name)
{
	char *slash, *dot;

	slash = name + strcspn(name, "/");
	if (*slash != '\0')
		name = slash + 1;

	if ((dot = strrchr(name, '.')) != NULL)
		*dot = '\0';
	if (*name == '\0')
		fatal("%s: cannot build font name", name);

        snprintf(buf, sz, "%s%s", flag_p, name);
	*dot = '.';

	return buf;
}

static void
txt2cfont(char *path)
{
	char buf[128];
	size_t h;

	memset(&ctx, 0, sizeof ctx);
	ctx.ascii = ASCII_FIRST;

	if ((ctx.fp = fopen(path, "r")) == NULL)
		fatal("%s: %s", ctx.fp, strerror(errno));

	printf("\n");
	if (flag_a != NULL)
		printf("%s\n", flag_a);
	printf("%s %s[] = {\n", flag_t, font_name(buf, sizeof buf, path));
	for (glyph_t g; parse_glyph(&g); h = g.height) {
		if (ctx.ascii == ASCII_FIRST)
			printf("\n\t/* height */ %zu,\n\n", g.height);
		else if (h != g.height)
			fatal("glyph '%c' of different height", ctx.ascii);
		print_glyph(&g);
		ctx.ascii++;
	}
	if (ctx.ascii <= '~')
		fatal("missing characters, next should be '%c'", ctx.ascii);
	printf("};\n");
}

char const *arg0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s"
	    " [-a attribute] [-i include] [-p prefix] [-t type] file\n",
	    arg0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	arg0 = *argv;
	for (int o; (o = getopt(argc, argv, "a:i:p:t:")) != -1;) {
		switch (o) {
		case 'a':
			flag_a = optarg;
			break;
		case 'i':
			printf("#include %s\n", optarg);
			break;
		case 'p':
			flag_p = optarg;
			break;
		case 't':
			flag_t = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	printf("\n");
	printf("/* generated by %s */\n", arg0);

	for (; *argv != NULL; argv++)
		txt2cfont(*argv);

	return 0;
}