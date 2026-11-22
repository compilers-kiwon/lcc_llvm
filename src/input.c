#include "c.h"

static char rcsid[] = "$Id$";

static void pragma(void);
static void resynch(void);

static int bsize;
static unsigned char buffer[MAXLINE+1 + BUFSIZE+1];
unsigned char *cp;	/* current input character */
char *file;		/* current input file name */
char *firstfile;	/* first input file */
unsigned char *limit;	/* points to last character + 1 */
char *line;		/* current line */
int lineno;		/* line number of current line */

void nextline(void) {
	do {
		if (cp >= limit) {
			fillbuf();
			if (cp >= limit)
				cp = limit;
			if (cp == limit)
				return;
		} else {
			lineno++;
			for (line = (char *)cp; *cp==' ' || *cp=='\t'; cp++)
				;
			if (*cp == '#') {
				resynch();
				nextline();
			}
		}
	} while (*cp == '\n' && cp == limit);
}
void fillbuf(void) {
	if (bsize == 0)
		return;
	if (cp >= limit)
		cp = &buffer[MAXLINE+1];
	else
		{
			int n = limit - cp;
			unsigned char *s = &buffer[MAXLINE+1] - n;
			assert(s >= buffer);
			line = (char *)s - ((char *)cp - line);
			while (cp < limit)
				*s++ = *cp++;
			cp = &buffer[MAXLINE+1] - n;
		}
	if (feof(stdin))
		bsize = 0;
	else
		bsize = fread(&buffer[MAXLINE+1], 1, BUFSIZE, stdin);
	if (bsize < 0) {
		error("read error\n");
		exit(EXIT_FAILURE);
	}
	limit = &buffer[MAXLINE+1+bsize];
	*limit = '\n';
}
void input_init(int argc, char *argv[]) {
	static int inited;

	if (inited)
		return;
	inited = 1;
	main_init(argc, argv);
	limit = cp = &buffer[MAXLINE+1];
	bsize = -1;
	lineno = 0;
	file = NULL;
	fillbuf();
	if (cp >= limit)
		cp = limit;
	nextline();
}

/* ident - handle #ident "string" */
static void ident(void) {
	while (*cp != '\n' && *cp != '\0')
		cp++;
}

/* idchar - is c a character of a name? */
static int idchar(int c) {
	return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z'
	    || c >= '0' && c <= '9' || c == '_';
}

/* isref - skip the first word of the #pragma at cp if it is ref; not
   with gettok, which reads on in the next line if this one has none */
static int isref(void) {
	unsigned char *s = cp;

	while (*s == ' ' || *s == '\t')
		s++;
	if (strncmp((char *)s, "ref", 3) != 0 || idchar(s[3]))
		return 0;
	cp = s + 3;
	return 1;
}

/* refs - count the names in the rest of the #pragma ref line at cp as
   used.  Not with gettok, which reads on in the next line after a \r
   (CRLF) or a comment: the line ends at its \n, also inside a comment
   or constant, which this skips */
static void refs(void) {
	Coordinate pos;
	unsigned char *s;
	Symbol p;
	int q = 0;	/* '*' in a comment, '"' or '\'' in a constant */

	for (;;) {
		if (limit - cp < MAXLINE)
			fillbuf();
		if (*cp == '\n' || *cp == 0)
			return;
		if (q == '*') {
			if (cp[0] == '*' && cp[1] == '/')
				cp++, q = 0;
			cp++;
		} else if (q) {
			if (*cp == '\\' && cp[1] != '\n' && cp[1] != 0)
				cp++;
			else if (*cp == q)
				q = 0;
			cp++;
		} else if (cp[0] == '/' && cp[1] == '*')
			cp += 2, q = '*';
		else if (*cp == '"' || *cp == '\'')
			q = *cp++;
		else if (*cp == 'L' && (cp[1] == '"' || cp[1] == '\''))
			cp++, q = *cp++;
		else if (*cp >= '0' && *cp <= '9'
		|| *cp == '.' && cp[1] >= '0' && cp[1] <= '9')
			for (cp++; idchar(*cp) || *cp == '.'	/* a number */
			|| (*cp == '+' || *cp == '-') && strchr("eEpP", cp[-1]); cp++)
				;
		else if (idchar(*cp)) {
			for (s = cp; idchar(*cp); cp++)
				;
			if ((p = lookup(stringn((char *)s, cp - s), identifiers))) {
				pos.file = file;
				pos.x = (char *)s - line;
				pos.y = lineno;
				p->ref++;
				use(p, pos);
			}
		} else
			cp++;
	}
}

/* pragma - handle #pragma ref id..., and #pragma NS ... of -pragma=NS */
static void pragma(void) {
	if (pragmans > 0 && nspragma())
		return;
	if (isref())
		refs();
}

/* resynch - set line number/file name in # n [ "file" ], #pragma, etc. */
static void resynch(void) {
	for (cp++; *cp == ' ' || *cp == '\t'; )
		cp++;
	if (limit - cp < MAXLINE)
		fillbuf();
	if (strncmp((char *)cp, "pragma", 6) == 0) {
		cp += 6;
		pragma();
	} else if (strncmp((char *)cp, "ident", 5) == 0) {
		cp += 5;
		ident();
	} else if (*cp >= '0' && *cp <= '9') {
	line:	for (lineno = 0; *cp >= '0' && *cp <= '9'; )
			lineno = 10*lineno + *cp++ - '0';
		lineno--;
		while (*cp == ' ' || *cp == '\t')
			cp++;
		if (*cp == '"') {
			file = (char *)++cp;
			while (*cp && *cp != '"' && *cp != '\n')
				cp++;
			file = stringn(file, (char *)cp - file);
			if (*cp == '\n')
				warning("missing \" in preprocessor line\n");
			if (firstfile == 0)
				firstfile = file;
		}
	} else if (strncmp((char *)cp, "line", 4) == 0) {
		for (cp += 4; *cp == ' ' || *cp == '\t'; )
			cp++;
		if (*cp >= '0' && *cp <= '9')
			goto line;
		if (Aflag >= 2)
			warning("unrecognized control line\n");
	} else if (Aflag >= 2 && *cp != '\n')
		warning("unrecognized control line\n");
	while (*cp)
		if (*cp++ == '\n')
			if (cp == limit + 1) {
				nextline();
				if (cp == limit)
					break;
			} else
				break;
}

