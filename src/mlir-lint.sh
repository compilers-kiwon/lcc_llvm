#!/bin/sh
# mlir-lint.sh - check the MLIR of -target=mlir/x86_64-linux against the
# rules of the back end
#
# usage: src/mlir-lint.sh file.mlir ...
#        src/mlir-lint.sh -t
#
# A file, as lcc -S writes it, must be MLIR that mlir-opt parses and
# verifies, and keep these rules:
#
# 1. Functions compute and branch with arith and cf, not with llvm ops for
#    arithmetic, comparison, conversion, select, branch or constants, and
#    do not use llvm.va_arg, which LLVM 21 gets wrong for x86_fp80 and
#    structs.
# 2. Functions call an llvm.func through llvm.mlir.addressof, never with a
#    direct llvm.call.
# 3. Nothing uses llvm.blockaddress, llvm.blocktag, llvm.indirectbr or the
#    ub dialect.
# 4. No floating-point constant is in decimal: MLIR reads a decimal
#    x86_fp80 through double, so rcc writes the bits in hex.
# 5. Every block is reachable from the entry block of its region; the
#    conversion passes do not lower the operations of the others.
#    Canonicalize with no rewrites erases unreachable blocks, also those
#    in a cycle, which mlir-opt does not mark "no predecessors", and keeps
#    the others: it must leave as many blocks.
#
# Functions are the regions of func.func and llvm.func definitions, from a
# line in column 0 that ends with { to a } in column 0; the initializers of
# llvm.mlir.global build their constants with llvm ops.  Text in strings
# and comments is not checked, nor are operations in the generic form,
# whose names are strings.  mlir-opt is $MLIRBIN/mlir-opt ($MLIRBIN is
# /usr/lib/llvm-21/bin by default).
#
# A violation is printed as file:line: and a message, or as file: and a
# message if it has no line; an error of mlir-opt at a line of the file
# is printed as mlir-opt prints it, file:line:column: error: and a
# message.  The exit status is 1 if there are any.
#
# -t checks this script with tst/mlir-lint/*.mlir instead.  A line there
# that must be reported ends with the comment "// mlir-lint: " and the
# start of its message; one line that is only such a comment, in column 0,
# gives the start of a message without a line.  Each of these messages
# must be reported once and nothing else, with exit status 1 if there are
# any.

usage() {
	sed -n '2,/^$/s/^# \{0,1\}//p' "$0" 1>&2
	exit 2
}
case "$1" in
-t)	[ $# = 1 ] || usage ;;
-*|'')	usage ;;
esac

MLIRBIN=${MLIRBIN-/usr/lib/llvm-21/bin}
t=`mktemp -d` || exit 1
trap 'rm -rf "$t"' 0
trap 'exit 1' 1 2 15

# mliropt arg ...: mlir-opt, with the location of every operation
mliropt() {
	"$MLIRBIN/mlir-opt" --mlir-print-debuginfo --mlir-print-local-scope "$@"
}

# lint file: print the violations in file; return 1 if there are any
lint() {
	if [ ! -r "$1" ]; then
		echo "$1: cannot read"
		return 1
	fi
	st=0
	# rules 1 to 4
	awk -v f="$1" '
	function bad(why) {
		line = $0
		sub(/^[ \t]+/, "", line)
		printf "%s:%d: %s: %s\n", f, FNR, why, line
		n++
	}
	BEGIN {	# the llvm ops of rule 1
		op = "llvm\\.(add|sub|mul|sdiv|udiv|srem|urem|and|or|xor|shl|" \
		    "lshr|ashr|icmp|fcmp|fadd|fsub|fmul|fdiv|frem|fneg|sext|" \
		    "zext|trunc|sitofp|uitofp|fptosi|fptoui|fpext|fptrunc|" \
		    "bitcast|select|br|cond_br|switch|va_arg|mlir\\.constant|" \
		    "mlir\\.undef|mlir\\.poison)[ (]"
	}
	{	# the line without its strings, and without its comment and
		# the blanks before that
		s = $0
		gsub(/"([^"\\]|\\.)*"/, "\"\"", s)
		sub(/[ \t]*\/\/.*/, "", s)
	}
	fn && s ~ op { bad("llvm op instead of arith or cf") }
	s ~ /llvm\.call @/ { bad("direct llvm.call") }
	s ~ /llvm\.(blockaddress|blocktag|indirectbr)|(^|=)[ \t]*ub\.[a-z]/ {
		bad("forbidden op")
	}
	s ~ /(^|[^%A-Za-z0-9_.$-])[-+]?[0-9]+\.[0-9]*([eE][-+]?[0-9]+)? *: *f(16|32|64|80|128)([^0-9]|$)/ {
		bad("decimal float literal")
	}
	s ~ /^(func\.func|llvm\.func) .*[{][ \t]*$/ { fn = 1 }
	/^[}]/ { fn = 0 }
	END { exit n > 0 }' "$1" || st=1
	# mlir-opt must parse and verify the file.  The canonicalized output
	# is for rule 5; region-simplify=aggressive would also merge blocks
	# that are the same.
	if ! mliropt "$1" >"$t/all" 2>"$t/err" ||
	   ! mliropt --canonicalize="max-num-rewrites=0 region-simplify=normal" \
		"$1" >"$t/reach" 2>"$t/err"; then
		# its errors at lines of the file, or else its first message
		awk -v f="$1" '
		NR == 1 { first = $0 }
		index($0, f ":") == 1 && / error: / { print; n++ }
		END {
			if (!n)
				print f ": mlir-opt fails" (NR ? ": " first : "")
		}' "$t/err"
		return 1
	fi
	# Rule 5.  In mlir-opt's output, the entry block of a region starts at
	# its {, another block at its label, and a block ends with its
	# terminator, the last operation before the next label or the } of
	# the region, known by its line in the file.  A block whose terminator
	# canonicalize drops is unreachable.  It is reported at its label if
	# that starts a line and no other block ends on the line of its
	# terminator; the blocks are counted as well.
	awk -v f="$1" '
	BEGIN {	# a label in the file, with its arguments and its colon
		lab = "\\^[A-Za-z0-9_.$-]+[ \t]*(:|\\(.*\\)[ \t]*:)"
	}
	FNR == 1 { k++ }
	k < 3 && /[{]$/ {
		nblocks[k]++
		last = ""
		next
	}
	k < 3 && (/^ *\^bb[0-9]+(:|\(.*\):)/ || /^ *[}]/) {
		if (last != "")
			term[k, last] = 1
		nblocks[k] += /^ *\^/
		last = ""
		next
	}
	k < 3 && match($0, /:[0-9]+:[0-9]+\)$/) {
		last = substr($0, RSTART + 1)
		sub(/:.*/, "", last)
	}
	# the label of the block of each line in the file, or 0; the entry
	# block of a region has none, and is always reachable
	k == 3 && /[{][ \t]*$/ { label = 0 }
	k == 3 && $0 ~ "^[ \t]*" lab {
		label = FNR
		name = $1
		sub(/[(:].*/, "", name)
	}
	k == 3 && label && ((1, FNR) in term) && !((2, FNR) in term) {
		printf "%s:%d: block %s is unreachable\n", f, label, name
		n++
	}
	# a block whose label is after something else on its line is not
	# named
	k == 3 && $0 ~ "[^ \t].*" lab { label = 0 }
	END {
		if (nblocks[1] - nblocks[2] != n)
			printf "%s: %d of %d blocks unreachable\n", f,
				nblocks[1] - nblocks[2], nblocks[1]
		exit n > 0 || nblocks[1] != nblocks[2]
	}' "$t/all" "$t/reach" "$1" || st=1
	return $st
}

if [ "$1" = -t ]; then
	d=`dirname "$0"`
	cd "$d/.." || exit 1
	npass=0 nfail=0
	for f in tst/mlir-lint/*.mlir; do
		lint "$f" >"$t/out" 2>&1
		if awk -v f="$f" -v st=$? '
		FILENAME == f {	# the messages the file must be reported with
			if (match($0, /\/\/ mlir-lint: /)) {
				l = RSTART == 1 ? 0 : FNR
				if (l in want) {
					print "\tmore than one message without a line"
					bad = 1
				}
				want[l] = substr($0, RSTART + RLENGTH)
				nwant++
			}
			next
		}
		{	# file:line: message, file:line:column: message from
			# mlir-opt, or file: message
			m = substr($0, length(f) + 2)
			l = 0
			if (match(m, /^[0-9]+:([0-9]+:)? /)) {
				l = m + 0
				m = substr(m, RLENGTH + 1)
			} else
				sub(/^ /, "", m)
			if (index($0, f ":") == 1 && (l in want) && !(l in seen) &&
			    index(m, want[l]) == 1)
				seen[l] = 1
			else {
				print "\tnot expected: " $0
				bad = 1
			}
		}
		END {
			for (l in want)
				if (!(l in seen)) {
					print "\tnot reported: " f ":" \
						(l + 0 ? l ":" : "") " " want[l]
					bad = 1
				}
			if (st != (nwant > 0)) {
				print "\texit status " st
				bad = 1
			}
			exit bad
		}' "$f" "$t/out" >"$t/why"; then
			npass=`expr $npass + 1`
			echo "PASS    `basename "$f" .mlir`"
		else
			nfail=`expr $nfail + 1`
			echo "FAIL    `basename "$f" .mlir`"
			cat "$t/why"
		fi
	done
	echo "mlir-lint: `expr $npass + $nfail` files, $npass passed, $nfail failed"
	[ $nfail = 0 ]
	exit
fi

status=0
for f in "$@"; do
	lint "$f" || status=1
done
exit $status
