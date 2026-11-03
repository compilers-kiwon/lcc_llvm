/* SysV x86-64 ABI and data formats shared by back ends; see sysv.c */

/* free argument registers while arguments are assigned to them */
struct regs {
	int gp, fp;
};

extern int sysv_structparts(Type, struct regs *, char *[]);
extern int sysv_retparts(Type, char *[]);
extern int sysv_ssepart(char *);
extern void sysv_scalarreg(char *, struct regs *);
extern int sysv_needsslot(Type);
extern int sysv_returnstwice(char *);
extern void sysv_fp80bits(long double, unsigned *, unsigned long *, unsigned long *);
