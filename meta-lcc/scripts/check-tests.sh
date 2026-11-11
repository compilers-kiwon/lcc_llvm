#!/bin/sh
# check-tests.sh machine... - check the lcc of meta-lcc's toolchain on
# each Yocto MACHINE with lcc_llvm's own tests
#
# Run it in a build directory, after oe-init-build-env, with meta-lcc
# added.  For each machine it builds lcc-hello, which stages lcc-cross,
# lcc-runtime, the cross toolchain and the target sysroot, runs lcc-hello
# under user-mode qemu, then hands that toolchain to src/run-llvm.sh
# (OESYSROOT) for its suites lcc and unit; the unit tests' drivers are
# compiled by the toolchain's gcc.  $SUITES picks other suites, e.g.
# SUITES="lcc unit c-testsuite torture llvm-bench" with the directories
# of the external ones in $C_TESTSUITE, $TORTURE and $LLVM_TESTSUITE.
# The results go to $OUT (default tmp/lcc-tests) as MACHINE/SUITE;
# the exit status is 0 only if every suite passes on every machine.
#
#   meta-lcc/scripts/check-tests.sh qemux86-64 qemuarm64 qemuriscv64 qemuarm qemuriscv32

REPO=`cd \`dirname $0\`/../.. && pwd`
OUT=${OUT-$PWD/tmp/lcc-tests}
SUITES=${SUITES-lcc unit}
[ $# -gt 0 ] || { sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2; exit 2; }
fails=0

# var name file: the value of name in bitbake -e's output
var() {
	sed -n "s/^\(export \)\{0,1\}$1=\"\(.*\)\"\$/\2/p" $2
}

for m; do
	echo "=== $m"
	mkdir -p $OUT/$m || exit 1
	if ! MACHINE=$m bitbake lcc-hello >$OUT/$m/bitbake.log 2>&1; then
		echo "bitbake lcc-hello failed: $OUT/$m/bitbake.log"
		fails=`expr $fails + 1`
		continue
	fi
	env=$OUT/$m/lcc-hello.env
	MACHINE=$m bitbake -e lcc-hello >$env || { fails=`expr $fails + 1`; continue; }
	sysroot=`var STAGING_DIR_TARGET $env`
	prefix=`var TARGET_PREFIX $env`
	# the unit tests' drivers declare functions the K&R way, which C23,
	# the default of gcc 16, no longer has
	cc="`var CC $env` -std=gnu89"
	# the toolchain (lcc, gcc) and llvm-native's llc before the host's tools
	path=`var STAGING_BINDIR_TOOLCHAIN $env`:`var STAGING_BINDIR_NATIVE $env`:$PATH
	arch=`var TARGET_ARCH $env`
	case $arch in
	arm)	target=armhf-linux q=arm
		# lcc's time_t has 32 bits (README); Yocto's 32-bit ARM has 64
		cc="$cc -U_TIME_BITS -D_FILE_OFFSET_BITS=64" ;;
	*)	target=$arch-linux q=$arch ;;
	esac

	# lcc-hello as it went into the package
	hello=`var WORKDIR $env`/build/lcc-hello
	got=`echo "270 192" | qemu-$q -L $sysroot -E LD_LIBRARY_PATH=/usr/lib:/lib $hello`
	if [ "$got" = "gcd 6" ]; then
		echo "lcc-hello: $got"
	else
		echo "lcc-hello: \"$got\", expected \"gcd 6\""
		fails=`expr $fails + 1`
	fi

	for s in $SUITES; do
		case $s in
		c-testsuite) dir=$C_TESTSUITE ;;
		torture) dir=$TORTURE ;;
		llvm-bench) dir=$LLVM_TESTSUITE ;;
		*) dir= ;;
		esac
		PATH=$path OESYSROOT=$sysroot LCC="${prefix}lcc --sysroot=$sysroot" \
			DRVCC="$cc" $REPO/src/run-llvm.sh -q -t $target \
			-o $OUT/$m/$s $s $dir >$OUT/$m/$s.log 2>&1
		status=$?
		tail -1 $OUT/$m/$s.log
		[ $status = 0 ] || fails=`expr $fails + 1`
	done
done
[ $fails = 0 ]
