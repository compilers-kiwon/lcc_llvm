/*
 * Shapes of control flow: code after return, break, continue and goto,
 * which no path reaches; goto forward and back, to the first statement
 * of a function and to consecutive labels; loops left only by return or
 * goto; functions that fall off their end.  Static functions have the
 * names of functions that flow-drv.c defines, so a static function
 * given external linkage makes the link fail.
 */
static int twice(int x) { return 2*x; }
static int count;
static void bump(int n) { count += n; }

int afterret(int a) {
	return a + 1;
	a = twice(a);
	return a;
}

int afterbreak(int n) {
	int i, s = 0;

	for (i = 0; i < n; i++) {
		if (i == 7) {
			break;
			s += 100;
		}
		if (i%2) {
			continue;
			s += 1000;
		}
		s += twice(i);
	}
	return s;
}

int entrylabel(int n, int s) {
again:
	if (n <= 0)
		return s;
	s += n--;
	goto again;
}

int labels(int k) {
	int x = 0;

	if (k == 1)
		goto a;
	if (k == 2)
		goto b;
	if (k == 3)
		goto c;
	x = 100;
a:
b:
c:
	x += k;
	return x;
}

int backward(int n) {
	int s = 0, i = 0;

top:
	if (i >= n)
		goto out;
	s += i*i;
	i++;
	goto top;
out:
	return s;
}

/* reaches the label after the return only by goto */
int gotodead(int n) {
	int s = 1;

	if (n > 3)
		goto late;
	return s;
late:
	s = n*3;
	goto done;
	s = -1;
done:
	return s;
}

int nested(int n) {
	int i, j, s = 0;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			if (i*j > 20)
				goto out;
			s += i + j;
		}
out:
	return s;
}

/* loops that only return leaves */
int forever(int n) {
	int i = 0;

	for (;;) {
		if (i*i >= n)
			return i;
		i++;
	}
}

int whileone(int n) {
	while (1) {
		if (n%5 == 0)
			return n;
		n++;
	}
	return -1;
}

int deadloops(int n) {
	while (0)
		n += 100;
	if (0) {
		n += 200;
	}
	do
		n++;
	while (0);
	for ( ; 0; )
		n += 300;
	return n;
}

int sparse(int k) {
	int s = 0;

	switch (k) {
	case 1:
		s = 10;
		break;
		s = 11;
	case 1000:
		s += 20;
	case 100000:
		s += 30;
		break;
	default:
		return -k;
		s = 1;
	}
	return s;
}

/* falls off its end; the caller does not use the value */
int noreturn(int *p, int v) {
	*p = v + 1;
}

void setcount(int n) {
	count = 0;
	if (n < 0)
		return;
	bump(n);
}

void nothing(void) {}

int getcount(void) {
	nothing();
	return count;
}

int whilegoto(int n) {
	int s = 0;

	while (n > 0) {
		if (n == 13)
			goto skip;
		s += n;
	skip:
		n--;
	}
	return s;
}
