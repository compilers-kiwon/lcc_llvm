#include "c.h"

static char rcsid[] = "$Id$";

/*
 * #pragma NS OP KEY=VALUE ..., for a name space NS given by -pragma=NS,
 * gives an operation and its arguments to the function whose body has
 * it.  A VALUE is the name of a file-level object, an integer that fits
 * in 64 bits, decimal or hexadecimal with an optional sign, a list of
 * such integers in parentheses, or a string.  cpp passes #pragma lines
 * as they are, without expanding macros.  The mlir back end emits the
 * pragmas as the attributes NS.op and NS.args of their functions; the
 * others check them only.
 *
 * A pragma goes between the declarations and statements of the outermost
 * block of a function: one for each name space.  The lexer reads a
 * #pragma line when it reads the token after it, while the parser is
 * still at the one before.  So a pragma waits: compound() takes it at a
 * boundary of the outermost block, where that token is next, and gettok
 * reports it if it is still waiting when that token is passed.  A line
 * that getchr reads, looking past the next token, comes before the token
 * after that one.
 */

int pragmans;		/* number of name spaces */
int pragmapending;	/* != 0 if pragmas wait */
int pragmapeek;		/* != 0 while getchr looks past the next token */

static List names;	/* the name spaces */
static Pragma pending;	/* the pragmas that wait, in source order */
static Pragma *pendtail = &pending;
static Symbol owner;	/* the function of the pragmas taken */
static int ownerlab;	/* its label, which tells definitions apart */
static Pragma taken;	/* the pragmas of owner, in source order */
static Pragma *taketail = &taken;

static char *lbuf;	/* the line of a pragma */
static int lsize;
static unsigned char *p;	/* its next character */
static unsigned char *end;	/* its end */
static char *ns;	/* its name space */

static char *keywords[] = {	/* those of C89 */
	"auto", "break", "case", "char", "const", "continue", "default",
	"do", "double", "else", "enum", "extern", "float", "for", "goto",
	"if", "int", "long", "register", "return", "short", "signed",
	"sizeof", "static", "struct", "switch", "typedef", "union",
	"unsigned", "void", "volatile", "while", NULL
};

static int isdigit_(int c) { return c >= '0' && c <= '9'; }
static int isxdigit_(int c) {
	return isdigit_(c) || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F';
}
static int isidstart(int c) {
	return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '_';
}
static int isidchar(int c) { return isidstart(c) || isdigit_(c); }
static int xvalue(int c) {
	return isdigit_(c) ? c - '0' : c >= 'a' ? c - 'a' + 10 : c - 'A' + 10;
}

/* iskeyword - is the interned string s a keyword of C89? */
static int iskeyword(char *s) {
	int i;

	for (i = 0; keywords[i]; i++)
		if (strcmp(s, keywords[i]) == 0)
			return 1;
	return 0;
}

/* isname - is s[0..n-1] an identifier that is not a keyword? */
static int isname(char *s, int n) {
	int i;

	if (n <= 0 || !isidstart(s[0]))
		return 0;
	for (i = 1; i < n; i++)
		if (!isidchar(s[i]))
			return 0;
	return !iskeyword(stringn(s, n));
}

/* enabled - is the interned string s a name space? */
static int enabled(char *s) {
	List lp = names;

	if (lp)
		do {
			lp = lp->link;
			if (lp->x == s)
				return 1;
		} while (lp != names);
	return 0;
}

/* pragmaname - add the name space s of -pragma=s; return 0 if it cannot be one */
int pragmaname(char *s) {
	int n = strlen(s);

	if (!isname(s, n) || strcmp(s, "ref") == 0)	/* #pragma ref id ... */
		return 0;
	s = stringn(s, n);
	if (!enabled(s)) {
		names = append(s, names);
		pragmans++;
	}
	return 1;
}

/* blanks - skip the blanks at p */
static void blanks(void) {
	while (*p == ' ' || *p == '\t' || *p == '\v' || *p == '\f' || *p == '\r')
		p++;
}

/* name - the identifier at p, interned */
static char *name(void) {
	unsigned char *s = p;

	assert(isidstart(*p));
	while (isidchar(*p))
		p++;
	return stringn((char *)s, p - s);
}

/* syntax - report a syntax error at p */
static void syntax(void) {
	int n;

	if (p == end) {
		error("syntax error at the end of `#pragma %s'\n", ns);
		return;
	}
	for (n = 0; p + n < end && n < 20; n++)
		;
	error("syntax error in `#pragma %s' at `%S'\n", ns, (char *)p, n);
}

/* number - the integer constant at p, a pp-number, with sign neg; its
   value in decimal, or NULL after an error */
static char *number(int neg) {
	unsigned char *s = p, *d;
	unsigned long m[4], c, r;	/* the magnitude, 16 bits a digit */
	char buf[24], *b = buf + sizeof buf;
	int i, j, n, base = 10, over = 0, zero;

	while (isidchar(*p) || *p == '.'
	|| (*p == '+' || *p == '-') && strchr("eEpP", p[-1]))
		p++;
	n = p - s;
	d = s;
	if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		d = s + 2;
	}
	for (i = d - s; i < n && (base == 16 ? isxdigit_(s[i]) : isdigit_(s[i])); i++)
		;
	if (i < n) {
		if (base == 10 && (memchr(s, '.', n) || memchr(s, 'e', n) || memchr(s, 'E', n))
		||  base == 16 && (memchr(s, '.', n) || memchr(s, 'p', n) || memchr(s, 'P', n)))
			error("floating-point constant `%S' in `#pragma %s'\n", (char *)s, n, ns);
		else if (i > d - s && strspn((char *)s + i, "uUlL") == n - i)
			error("suffix on integer `%S' in `#pragma %s'\n", (char *)s, n, ns);
		else
			error("invalid constant `%S' in `#pragma %s'\n", (char *)s, n, ns);
		return NULL;
	}
	if (base == 10 && n > 1 && s[0] == '0') {
		error("octal constant `%S' in `#pragma %s'\n", (char *)s, n, ns);
		return NULL;
	}
	m[0] = m[1] = m[2] = m[3] = 0;
	for ( ; d < p; d++) {
		c = xvalue(*d);
		for (j = 0; j < 4; j++) {
			c += m[j]*base;
			m[j] = c&0xFFFF;
			c >>= 16;
		}
		if (c)
			over = 1;
	}
	zero = (m[0]|m[1]|m[2]|m[3]) == 0;
	if (over || m[3] > 0x8000 || m[3] == 0x8000 && (!neg || m[2]|m[1]|m[0])) {
		error("integer `%s%S' out of range in `#pragma %s'\n",
			neg ? "-" : "", (char *)s, n, ns);
		return NULL;
	}
	*--b = 0;
	do {
		for (r = 0, j = 3; j >= 0; j--) {
			r = r<<16 | m[j];
			m[j] = r/10;
			r %= 10;
		}
		*--b = '0' + r;
	} while (m[0]|m[1]|m[2]|m[3]);
	if (neg && !zero)
		*--b = '-';
	return string(b);
}

/* integer - the integer at p, with an optional sign; NULL after an error */
static char *integer(void) {
	int neg = 0;

	if (*p == '-' || *p == '+') {
		neg = *p++ == '-';
		blanks();
	}
	if (isdigit_(*p) || *p == '.' && isdigit_(p[1]))
		return number(neg);
	if (*p == '\'' || *p == 'L' && p[1] == '\'')
		error("character constant in `#pragma %s'\n", ns);
	else
		syntax();
	return NULL;
}

/* intlist - the list of integers at p, just after (; 0 after an error */
static int intlist(Pragmaarg a) {
	List l = NULL;
	char *s;

	blanks();
	if (*p != ')')
		for (;;) {
			if (*p == '(') {
				error("nested list in `#pragma %s'\n", ns);
				return 0;
			}
			if ((s = integer()) == NULL)
				return 0;
			l = append(s, l);
			blanks();
			if (*p == ')')
				break;
			if (p == end) {
				error("missing `)' in `#pragma %s'\n", ns);
				return 0;
			}
			if (*p != ',') {
				syntax();
				return 0;
			}
			p++;
			blanks();
		}
	p++;
	a->kind = PLIST;
	a->n = length(l);
	a->ints = ltov(&l, PERM);
	return 1;
}

/* strval - the string at p, just after "; 0 after an error */
static int strval(Pragmaarg a) {
	char *s = allocate(end - p + 1, PERM);
	unsigned c;
	int n = 0, i;

	for (;;) {
		if (p == end) {
			error("missing \" in `#pragma %s'\n", ns);
			return 0;
		}
		c = *p++;
		if (c == '"')
			break;
		if (c == '\\') {
			if (p == end)
				continue;	/* missing " */
			switch (c = *p++) {
			case 'a': c = 7; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'v': c = '\v'; break;
			case '\'': case '"': case '\\': case '?': break;
			case 'x':
				if (!isxdigit_(*p)) {
					error("ill-formed hexadecimal escape sequence in `#pragma %s'\n", ns);
					return 0;
				}
				for (c = 0; isxdigit_(*p); p++)
					if ((c = (c<<4) + xvalue(*p)) > 0xFF) {
						error("escape sequence out of range in `#pragma %s'\n", ns);
						return 0;
					}
				break;
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				c -= '0';
				for (i = 1; i < 3 && *p >= '0' && *p <= '7'; i++)
					c = (c<<3) + *p++ - '0';
				if (c > 0xFF) {
					error("escape sequence out of range in `#pragma %s'\n", ns);
					return 0;
				}
				break;
			default:
				if (c > ' ' && c < 0177)
					error("invalid escape sequence `\\%c' in `#pragma %s'\n", c, ns);
				else
					error("invalid escape sequence in `#pragma %s'\n", ns);
				return 0;
			}
		}
		s[n++] = c;
	}
	blanks();
	if (*p == '"' || *p == 'L' && p[1] == '"') {
		error("adjacent strings in `#pragma %s'\n", ns);
		return 0;
	}
	a->kind = PSTRING;
	a->s = s;
	a->n = n;
	return 1;
}

/* object - the file-level object named id; NULL after an error */
static Symbol object(char *id) {
	Symbol q = lookup(id, identifiers);

	if (q == NULL) {
		error("undeclared identifier `%s' in `#pragma %s'\n", id, ns);
		return NULL;
	}
	if (q->scope >= LOCAL && q->sclass == EXTERN)
		q = q->u.alias;		/* the object of a block-scope extern */
	if (q->scope != GLOBAL || q->sclass == TYPEDEF || q->sclass == ENUM
	|| isfunc(q->type)) {
		error("`%s' in `#pragma %s' is not a file-scope object\n", id, ns);
		return NULL;
	}
	return q;
}

/* argvalue - the value of argument a at p; 0 after an error */
static int argvalue(Pragmaarg a) {
	char *s;

	if (*p == '(') {
		p++;
		return intlist(a);
	} else if (*p == '"') {
		p++;
		return strval(a);
	} else if (*p == 'L' && p[1] == '"') {
		error("wide string in `#pragma %s'\n", ns);
		return 0;
	} else if (isidstart(*p) && !(*p == 'L' && p[1] == '\'')) {
		s = name();
		if (iskeyword(s)) {
			error("keyword `%s' in `#pragma %s'\n", s, ns);
			return 0;
		}
		if ((a->sym = object(s)) == NULL)
			return 0;
		a->kind = PIDENT;
		return 1;
	} else if (p == end) {
		error("missing value of `%s' in `#pragma %s'\n", a->key, ns);
		return 0;
	} else if ((s = integer()) == NULL)
		return 0;
	a->kind = PINT;
	a->n = 1;
	a->ints = allocate(sizeof *a->ints, PERM);
	a->ints[0] = s;
	return 1;
}

/* parse - the pragma of ns at p, after the name space; NULL after an error */
static Pragma parse(void) {
	Pragma pr;
	Pragmaarg a, *tail;
	char *key;

	NEW0(pr, PERM);
	pr->ns = ns;
	blanks();
	if (p == end) {
		error("missing operation in `#pragma %s'\n", ns);
		return NULL;
	}
	if (!isidstart(*p)) {
		syntax();
		return NULL;
	}
	if (iskeyword(pr->op = name())) {
		error("keyword `%s' in `#pragma %s'\n", pr->op, ns);
		return NULL;
	}
	tail = &pr->args;
	for (;;) {
		blanks();
		if (p == end)
			break;
		if (!isidstart(*p)) {
			syntax();
			return NULL;
		}
		if (iskeyword(key = name())) {
			error("keyword `%s' in `#pragma %s'\n", key, ns);
			return NULL;
		}
		for (a = pr->args; a; a = a->link)
			if (a->key == key) {
				error("duplicate key `%s' in `#pragma %s'\n", key, ns);
				return NULL;
			}
		blanks();
		if (*p != '=') {
			error("missing `=' after `%s' in `#pragma %s'\n", key, ns);
			return NULL;
		}
		p++;
		blanks();
		NEW0(a, PERM);
		a->key = key;
		if (!argvalue(a))
			return NULL;
		*tail = a;
		tail = &a->link;
	}
	return pr;
}

/* readline - read the rest of the line at cp into lbuf; return its length */
static int readline(void) {
	int n = 0;

	for (;;) {
		if (cp >= limit) {	/* at the end of the buffer */
			fillbuf();
			if (cp >= limit)
				break;
		}
		if (*cp == '\n')
			break;
		if (n + 1 >= lsize) {
			lsize = 2*lsize + 128;
			if ((lbuf = realloc(lbuf, lsize)) == NULL) {
				error("insufficient memory\n");
				exit(1);
			}
		}
		lbuf[n++] = *cp++;
	}
	if (lsize == 0 && (lbuf = malloc(lsize = 128)) == NULL) {
		error("insufficient memory\n");
		exit(1);
	}
	lbuf[n] = 0;
	return n;
}

/* nspragma - handle the #pragma at cp, just after "pragma", if its name
   space is one of -pragma; return 0 if it is not */
int nspragma(void) {
	unsigned char *s = cp;
	Coordinate save = src;
	Pragma pr;
	int n;

	if (*s != ' ' && *s != '\t')
		return 0;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!isidstart(*s))
		return 0;
	for (n = 1; isidchar(s[n]); n++)
		;
	ns = stringn((char *)s, n);
	if (s + n >= limit || !enabled(ns))
		return 0;
	src.file = file;
	src.x = (char *)s - line;
	src.y = lineno;
	cp = s + n;
	n = readline();
	p = (unsigned char *)lbuf;
	end = p + n;
	if (cfunc == NULL || level < LOCAL)
		error("`#pragma %s' outside a function body\n", ns);
	else if (level > LOCAL)
		error("`#pragma %s' not between the declarations and statements of the outermost block of `%s'\n",
			ns, cfunc->name);
	else if ((pr = parse()) != NULL) {
		pr->src = src;
		pr->func = cfunc->name;
		pr->peeked = pragmapeek;
		*pendtail = pr;
		pendtail = &pr->link;
		pragmapending = 1;
	}
	src = save;
	return 1;
}

/* pragmatoken - gettok is to read a token: the pragmas that wait came
   before the token after them, which was not at a boundary, unless
   getchr read them, which come before the token to read */
void pragmatoken(void) {
	Coordinate save = src;
	Pragma pr, *pp = &pending;

	while ((pr = *pp) != NULL)
		if (pr->peeked) {
			pr->peeked = 0;
			pp = &pr->link;
		} else {
			*pp = pr->link;
			src = pr->src;
			error("`#pragma %s' not between the declarations and statements of the outermost block of `%s'\n",
				pr->ns, pr->func);
		}
	pendtail = pp;
	pragmapending = pending != NULL;
	src = save;
}

/* pragmaclaim - compound() is at a boundary between the declarations
   and statements of a block: the pragmas that wait, but not those that
   getchr read, are of cfunc if the block is its outermost one */
void pragmaclaim(void) {
	Coordinate save = src;
	Pragma pr, q, *pp = &pending;
	Pragmaarg a;

	if (pending == NULL || level != LOCAL)
		return;
	if (owner != cfunc || ownerlab != cfunc->u.f.label) {
		owner = cfunc;
		ownerlab = cfunc->u.f.label;
		taken = NULL;
		taketail = &taken;
	}
	while ((pr = *pp) != NULL) {
		if (pr->peeked) {
			pp = &pr->link;
			continue;
		}
		*pp = pr->link;
		pr->link = NULL;
		for (q = taken; q && q->ns != pr->ns; q = q->link)
			;
		if (q) {
			src = pr->src;
			error("more than one `#pragma %s' in `%s'\n", pr->ns, pr->func);
			continue;
		}
		for (a = pr->args; a; a = a->link)
			if (a->kind == PIDENT) {
				a->sym->ref++;
				use(a->sym, pr->src);
			}
		*taketail = pr;
		taketail = &pr->link;
	}
	pendtail = pp;
	pragmapending = pending != NULL;
	src = save;
}

/* funcpragmas - the pragmas of function f, in source order */
Pragma funcpragmas(Symbol f) {
	return owner == f && ownerlab == f->u.f.label ? taken : NULL;
}
