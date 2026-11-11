# lcc_llvm — LLVM·MLIR 백엔드를 단 lcc 4.2

[![CI](https://github.com/compilers-kiwon/lcc_llvm/actions/workflows/ci.yml/badge.svg)](https://github.com/compilers-kiwon/lcc_llvm/actions/workflows/ci.yml)

lcc는 Christopher W. Fraser와 David R. Hanson이 만든 재타깃 가능한 C89("ANSI C") 컴파일러다.
이 저장소는 [lcc 4.2](https://github.com/drh/lcc)에 백엔드 둘을 더했다. 하나는 **LLVM IR**을,
하나는 **MLIR**을 내며, 리눅스 다섯과 OS 없는 Cortex-M3까지 타깃은 여섯이다. 언어는 그대로 C89다.

- [원본 lcc 4.2 README(번역)](#원본-lcc-42-readme-번역)
- [lcc 4.x 코드 생성 인터페이스](https://drh.github.io/lcc/documents/interface4.pdf) — 백엔드가 받는 중간 코드
- [meta-lcc](meta-lcc/README.md) — lcc를 Yocto 교차 툴체인에 넣는 레이어
- [CPYRIGHT](CPYRIGHT) — lcc의 라이선스

## 시작하기

**릴리스 파일로.** 빌드할 것 없이 x86_64 리눅스에서 바로 쓴다. 타깃·백엔드마다 압축 파일이 하나씩
[Releases](https://github.com/compilers-kiwon/lcc_llvm/releases)에 있다. llvm 드라이버는 clang 18이, mlir
드라이버는 LLVM 21이 있어야 한다([필요한 것](#필요한-것-ubuntu-2404-기준)).

```bash
curl -fsSLO https://github.com/compilers-kiwon/lcc_llvm/releases/download/v4.2-llvm.1/lcc-v4.2-llvm.1-llvm-x86_64-linux.tar.gz
tar -xzf lcc-v4.2-llvm.1-llvm-x86_64-linux.tar.gz
L=$PWD/lcc-v4.2-llvm.1-llvm-x86_64-linux
LCCDIR=$L $L/lcc -o hello hello.c && ./hello
```

**x86_64 리눅스에서 소스로.** clang 18과 make가 있으면 된다.

```bash
git clone https://github.com/compilers-kiwon/lcc_llvm.git && cd lcc_llvm
B=$PWD/build; mkdir -p $B
make BUILDDIR=$B HOSTFILE=etc/llvm-x86_64-linux.c all headers
LCCDIR=$B $B/lcc -o hello hello.c && ./hello
```

**교차 타깃.** 같은 방법에 호스트 파일만 바꾼다. [빌드](#빌드)를 보라.

**Yocto로.** lcc를 툴체인에 넣어 레시피에서 쓰려면
[meta-lcc/BUILDING.md](meta-lcc/BUILDING.md)를 보라.

## 설계

| 결정 | 선택 |
| :- | :- |
| 프론트엔드 | lcc 4.2 그대로(C89). 표준이 컴파일러에 맡긴 것은 gcc·clang을 따름 |
| 백엔드 출력 | 텍스트 `.ll`(`-target=llvm/T`) 또는 `.mlir`(`-target=mlir/T`). LLVM 라이브러리를 링크하지 않음 |
| 오브젝트 | llvm: clang 18 / mlir: LLVM 21의 `mlir-opt` → `mlir-translate` → clang |
| 링크 | clang(교차 타깃은 lld) / Cortex-M3는 `arm-none-eabi-gcc` |
| ABI | 구조체 인자·결과, 가변 인자, long double, 비트 필드를 타깃 ABI대로. gcc·clang 오브젝트와 섞어 링크 가능 |
| MLIR 방언 | 값과 제어는 `func`·`arith`·`cf`, 메모리·전역·ABI는 `llvm` |
| long double 상수 | IEEE quad 타깃은 호스트 long double이 아니라 소프트웨어 quad(`src/quad.c`)로 계산 |

## 타깃

| 타깃 | ABI | C 라이브러리 | long double | 실행 |
| :- | :- | :- | :- | :- |
| `x86_64-linux` | System V AMD64 | glibc | x87 80비트 | 호스트 |
| `aarch64-linux` | AAPCS64 | glibc | IEEE quad | `qemu-aarch64` |
| `riscv64-linux` | LP64D | glibc | IEEE quad | `qemu-riscv64` |
| `armhf-linux` | AAPCS-VFP | glibc | double | `qemu-arm` ([주의](#armhf의-off_t와-time_t)) |
| `riscv32-linux` | ILP32D | glibc | IEEE quad | `qemu-riscv32` |
| `cortexm3-eabi` | AAPCS, OS 없음 | newlib | double | `qemu-system-arm -M mps2-an385`, semihosting |

타깃 `T`마다 드라이버 호스트 파일이 둘 있다. `etc/llvm-T.c`와 `etc/mlir-T.c`이다.

## 빌드

### 필요한 것 (Ubuntu 24.04 기준)

| 대상 | 패키지 |
| :- | :- |
| 모든 타깃 | gcc(또는 cc), make |
| `llvm/*` | clang 18(`/usr/bin/clang`), 교차 타깃은 lld |
| `mlir/*` | LLVM 21(`/usr/lib/llvm-21/bin`) |
| aarch64, riscv64, armhf | `libc6-dev-{arm64,riscv64,armhf}-cross`, 그 gcc(liblcc용), `qemu-user` |
| riscv32 | glibc·rv32gc·ilp32d인 `riscv32-unknown-linux-gnu` 툴체인([riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain)). Ubuntu 패키지 없음 |
| cortexm3-eabi | `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`, `qemu-system-arm` |

### 빌드 디렉터리

타깃과 백엔드마다 빌드 디렉터리 `B`를 따로 둔다. `headers`는 그 타깃의 자체 헤더
(`include/llvm/T`: `stdarg.h`, `stddef.h`, `limits.h`, `float.h` 등)를 `$B/include`로 복사한다.

```bash
make BUILDDIR=$B HOSTFILE=etc/llvm-x86_64-linux.c all headers     # 또는 etc/mlir-x86_64-linux.c
```

교차 타깃의 `liblcc`는 그 타깃의 컴파일러로 빌드한다.

```bash
make BUILDDIR=$B HOSTFILE=etc/llvm-aarch64-linux.c rcc lcc cpp headers
make BUILDDIR=$B CC=aarch64-linux-gnu-gcc liblcc
```

| 타깃 | 더 필요한 것 |
| :- | :- |
| `riscv32-linux` | 툴체인을 `LCCDIR/riscv32`에서 찾는다(링크여도 됨). liblcc는 `CC="$B/riscv32/bin/riscv32-unknown-linux-gnu-gcc -fPIE"` |
| `cortexm3-eabi` | 보드 파일(링커 스크립트, 벡터 표)을 `board`로 설치. gcc의 enum은 lcc처럼 4바이트로(`-fno-short-enums`) |

```bash
make BUILDDIR=$B HOSTFILE=etc/llvm-cortexm3-eabi.c rcc lcc cpp headers board
make BUILDDIR=$B CC="arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -fno-short-enums" liblcc
```

### 경로 매크로

드라이버가 부르는 도구의 경로는 매크로라서 빌드할 때 바꿀 수 있다.
예: `make ... CFLAGS='-DCLANG=\"/usr/bin/clang-18\"'`

| 매크로 | 기본값 | 쓰임 |
| :- | :- | :- |
| `LCCDIR` | `/usr/local/lib/lcc/` | cpp, rcc, lcc 헤더, liblcc가 있는 곳 |
| `CLANG` | `/usr/bin/clang` | llvm 드라이버의 오브젝트 생성과 링크 |
| `LLVMBIN` | `/usr/lib/llvm-21/bin` | mlir 드라이버의 `mlir-opt`, `mlir-translate`, clang |
| `SYSROOT` | `/usr/aarch64-linux-gnu` 등 | 교차 타깃의 C 라이브러리 헤더 |
| `ARMGCC` | `/usr/bin/arm-none-eabi-gcc` | cortexm3-eabi의 링크 |

### 설치

`$B`의 `rcc`, `cpp`, `liblcc.a`, `include/`(cortexm3-eabi는 `cortexm3.ld`, `vectors.o`도)를 `LCCDIR`로,
`lcc`를 `PATH`에 있는 디렉터리로 복사한다. 설치하지 않고 쓰려면 환경 변수 `LCCDIR`을 `$B`로 하거나
`lcc -Wo-lccdir=$B`를 준다.

## 사용

```bash
lcc -o prog prog.c          # 컴파일하고 링크
lcc -S prog.c               # prog.ll (mlir 드라이버는 prog.mlir)
lcc -c prog.c               # prog.o
lcc -Wa-O2 -o prog prog.c   # IR을 clang -O2로 최적화
```

| 옵션 | 뜻 |
| :- | :- |
| `-S` | rcc가 낸 IR을 `.ll`(mlir 드라이버는 `.mlir`)로 남기고 멈춤 |
| `-Wa<opt>` | 오브젝트를 만드는 clang에 넘김(예: `-Wa-O2`) |
| `-Wf<opt>` | rcc에 넘김 |
| `-Wp<opt>` | cpp에 넘김 |
| `-Wl<opt>`, `-Wl,a,b` | 링크하는 clang·gcc에 넘김(gcc식 `-Wl,`는 통째로) |
| `-Wo-lccdir=dir` | `LCCDIR` 대신 `dir`을 씀 |
| `-n` | 널 포인터 역참조 검사 코드를 넣음(liblcc의 `_YYnull`) |
| `-v` | 실행하는 명령을 보임 |

`-O`는 무시한다. 최적화는 `-Wa-O2`처럼 clang에 맡긴다.

Cortex-M3 프로그램은 qemu의 mps2-an385 보드에서 semihosting으로 표준 입출력과 종료 코드를 그대로 쓴다.

```bash
qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -kernel prog
```

## lcc 4.2와 다른 점

표준이 컴파일러에 맡긴 것은 gcc·clang을 따랐다.

- `__func__`(C99 6.4.2.2). assert 메시지에도 함수 이름이 들어간다.
- 매개변수가 하나인 매크로를 `()`로 부르면 빈 인자 하나로 본다.
- enum 형은 그 값을 담는 정수형(음수 상수가 없으면 `unsigned int`)과 호환된다.
- 비트 필드를 gcc와 같게 배치한다.
- cpp가 C99 가변 인자 매크로를 받는다(시스템 헤더 밖에서는 경고). newlib 헤더가 쓴다.
- 부동소수점 상수의 넘침·아래 넘침 경고가 gcc·clang과 같다.

나머지 변경은 원본의 버그 수정이며 git 이력에 있다. 64비트 호스트에서 드러나는 것, `gcc -O2`로
빌드할 때 드러나는 것, ASan·UBSan으로 찾은 정의되지 않은 동작, 상수 표의 0.0, 파일 수준 암묵적 선언,
원래 백엔드(x86/linux, sparc)의 레지스터 할당 등이다.

## 시험

`src/run-llvm.sh`가 컴파일하고 실행하고 결과를 확인한다. 옵션은 스크립트 첫머리의 주석에 있다.

| 묶음 | 내용 | 수 |
| :- | :- | :- |
| `lcc` | lcc 자체의 `tst/*.c`. 표준 출력과 컴파일러 메시지를 기준 파일과 비교 | 19 |
| `unit` | `tst/llvm/NAME.c`를 lcc로, `NAME-drv.c`를 clang이나 gcc로 컴파일해 링크하고 결과 확인 | 25 |
| `c-testsuite` | [c-testsuite](https://github.com/c-testsuite/c-testsuite)의 C89 시험 | 144 |
| `torture` | GCC torture 시험(LLVM test-suite 안)의 C89 시험 | 618 |
| `llvm-bench` | [LLVM test-suite](https://github.com/llvm/llvm-test-suite)의 벤치마크 | 26 |

외부 묶음은 이 저장소에 복사하지 않았다. 체크아웃한 위치를 준다. C99 기능을 쓰는 시험은 뺀다.

```bash
src/run-llvm.sh -b $B lcc
src/run-llvm.sh -b $B -t aarch64-linux unit
src/run-llvm.sh -b $B -q c-testsuite /path/to/c-testsuite
LCCFLAGS=-Wa-O2 DRVCC=gcc src/run-llvm.sh -b $B unit     # 최적화한 lcc 코드, gcc로 만든 드라이버
```

| 스크립트 | 하는 일 |
| :- | :- |
| `src/selfhost.sh` | lcc를 lcc 자신으로 두 번 빌드해 2단과 3단의 IR이 같은지 비교 |
| `src/cmp-backends.sh` | llvm·mlir 두 백엔드의 결과를 비교하고 서로 섞어 링크 |
| `src/mlir-lint.sh` | mlir 백엔드 출력이 방언 규칙을 지키는지 검사 |
| `src/run-x86.sh` | `tst/*.c`를 원래의 x86/linux 백엔드로 실행 |
| `meta-lcc/scripts/check-tests.sh` | Yocto 머신마다 툴체인의 lcc로 위 묶음을 실행 |

## CI/CD

GitHub Actions가 `master`에 push하거나 PR을 열 때마다 [ci.yml](.github/workflows/ci.yml)을 돌린다.

| 작업 | 하는 일 |
| :- | :- |
| `llvm/T`, `mlir/T` | 여섯 타깃 × 두 백엔드로 빌드하고 위 시험 묶음 전부, unit의 변형 셋(gcc 드라이버, 최적화한 드라이버, `-Wa-O2`), selfhost(Cortex-M3 빼고) |
| ASan + UBSan | lcc 자체(cpp, rcc, 드라이버)를 ASan·UBSan으로 빌드해 x86_64의 두 백엔드로 위 시험 묶음을 돌린다. 첫 보고에서 멈춘다 |
| x86_64 extras | 원래 x86/linux 백엔드(`run-x86.sh`), 두 백엔드 비교(`cmp-backends.sh`), `mlir-lint.sh -t` |
| riscv32 toolchain | Ubuntu에 없는 riscv32 툴체인을 빌드해 캐시에 둔다(처음 한 번 한 시간쯤) |
| Yocto layer | meta-lcc를 openembedded-core로 파싱하고 레시피가 무엇으로 풀리는지 확인 |

외부 시험 묶음은 정해 둔 커밋으로 CI에서 받는다([fetch-suites.sh](.github/scripts/fetch-suites.sh)).
타깃 하나의 설치·빌드는 [ci-target.sh](.github/scripts/ci-target.sh)가 하므로 로컬에서도 같은 명령으로 재현할 수 있다.

`v`로 시작하는 태그를 push하면 [release.yml](.github/workflows/release.yml)이 타깃·백엔드마다 lcc를 빌드하고
시험한 뒤 압축 파일(`lcc-TAG-BACKEND-TARGET.tar.gz`)로 GitHub Release에 올린다. 첫 릴리스는
[v4.2-llvm.1](https://github.com/compilers-kiwon/lcc_llvm/releases/tag/v4.2-llvm.1)이다.

## Yocto

[meta-lcc](meta-lcc/README.md)는 lcc를 OpenEmbedded 빌드의 교차 툴체인에 `TARGET_PREFIX-lcc`로 넣는
레이어다. 레시피가 gcc처럼 lcc로 타깃 프로그램을 컴파일할 수 있다. MLIR 백엔드를 켜면
`TARGET_PREFIX-lcc-mlir`도 생긴다. 오브젝트는 Yocto의 `llc`(MLIR은 `mlir-opt`, `mlir-translate`를 거쳐)가
만들고 링크는 툴체인의 gcc가 한다. 리눅스 타깃 다섯을 지원하며, qemux86-64, qemuarm64, qemuriscv64,
qemuarm, qemuriscv32에서 두 드라이버 모두 위 시험 묶음이 전부 통과한다.

## armhf의 off_t와 time_t

`armhf-linux`에서 lcc는 `_FILE_OFFSET_BITS=64`로 컴파일하므로 `off_t`는 64비트, `time_t`는 32비트다.
**이 타깃에서 lcc가 만든 오브젝트와 링크할 수 있는 것은 다음으로 빌드한 오브젝트뿐이다.**

```
clang --target=arm-linux-gnueabihf -D_FILE_OFFSET_BITS=64
arm-linux-gnueabihf-gcc -U_TIME_BITS -D_FILE_OFFSET_BITS=64
```

기본 설정에서 clang의 `off_t`는 32비트이고, Ubuntu gcc의 `time_t`는 64비트다(그 gcc는
`_TIME_BITS=64`와 `_FILE_OFFSET_BITS=64`를 미리 정의하며, `-U_TIME_BITS`는 둘 다 지운다). 그런
오브젝트와 lcc 오브젝트 사이에 `off_t`, `time_t`, 또는 이를 담은 형(`struct stat`, `fpos_t`,
`struct timeval` 등)을 주고받으면 틀린다. 인터페이스에 `time_t`를 쓰는 Ubuntu armhf 라이브러리도
64비트 `time_t`로 빌드되어 있어 마찬가지다. glibc 자체는 lcc 오브젝트와 잘 동작한다.

lcc는 64비트 `time_t`를 쓸 수 없다. glibc가 64비트 시간 함수를 GNU `__asm__` 레이블로 고르는데
lcc에는 그것이 없다.

## 제약

lcc는 C89를 컴파일하며, glibc와 리눅스 헤더가 쓰는 이후 C와 GNU C의 확장 일부를 받지 않는다.

| 확장 | 쓰는 헤더 |
| :- | :- |
| `long long` 상수(`0xff00000000000000ull`) | `bits/byteswap.h` |
| 이름 없는 구조체·공용체 멤버 | 32비트 타깃의 `bits/struct_mutex.h`, `bits/sigcontext.h`, RISC-V 32의 `bits/termios-struct.h` |
| 크기 0인 배열 | `bits/fcntl-linux.h`, RISC-V 64의 `sys/ucontext.h` |
| 이름 붙은 가변 인자 매크로 매개변수(`MEMBERS...`) | `linux/stddef.h` |
| gcc·clang이 미리 정의하는 `__SIZE_TYPE__` | `glob.h` |

그래서 리눅스 타깃에서 lcc 드라이버가 정의하는 기능 매크로(`_POSIX_SOURCE`)로는 C89 헤더와 대부분의
POSIX.1 헤더가 동작하지만, `netinet/in.h`와 `glob.h`는 안 되고, armhf와 RISC-V 32의 `pthread.h`,
RISC-V 32의 `termios.h`도 안 된다. `_DEFAULT_SOURCE`나 `_GNU_SOURCE`를 정의하거나, 32비트 타깃과
RISC-V 64에서 `_POSIX_C_SOURCE=200809L`이나 `_XOPEN_SOURCE=700`을 정의하면 실패하는 헤더가 더
늘어난다. `signal.h`, `stdlib.h`, `sys/types.h`, `sys/wait.h`, `sys/socket.h`, `fcntl.h`,
`semaphore.h`, `regex.h` 등이다.

## 디렉터리

| 경로 | 내용 |
| :- | :- |
| `src/llvm.c`, `src/mlir.c` | 두 백엔드 |
| `src/target.h` | 타깃 표(트리플, 데이터 배치, 크기, ABI) |
| `src/abi.c`, `sysv.c`, `aapcs64.c`, `arm.c`, `riscv.c` | 타깃별 호출 규약 |
| `src/quad.c` | 소프트웨어 IEEE quad |
| `src/*.c`(그 밖), `cpp/`, `lburg/` | lcc 4.2의 프론트엔드, 전처리기, 원래 백엔드 |
| `etc/llvm-T.c`, `etc/mlir-T.c` | 타깃별 드라이버 호스트 파일 |
| `etc/oe-linux.c` | Yocto 툴체인용 드라이버 |
| `etc/cortexm3/` | Cortex-M3 보드 파일 |
| `include/llvm/T/` | 타깃별 자체 헤더 |
| `tst/` | lcc 자체의 시험(`tst/*.c`, 입력 `.0`) |
| `tst/llvm/` | 단위 시험 |
| `llvm/T/tst/` | 타깃별 `tst/*.c` 기준 출력 |
| `meta-lcc/` | Yocto 레이어 |
| `.github/workflows/`, `.github/scripts/` | CI·릴리스 워크플로와 그 스크립트 |

## 원본 lcc 4.2 README (번역)

> 이 디렉터리는 lcc 4.2 배포판이다.
>
> lcc 3.x는 책 "A Retargetable C Compiler: Design and Implementation"(Addison-Wesley, 1995,
> ISBN 0-8053-1670-1)에 설명되어 있다. 3.x와 4.x는 크게 다르며, 가장 큰 차이는 중간 코드다. 자세한
> 것은 https://drh.github.io/lcc/documents/interface4.pdf 를 보라.
>
> 4.2판은 이전 판의 lcc와 호환되지 않는다. 이 배포판을 3.x 배포판 위에 풀지 말 것.
>
> LCC는 재타깃이 쉽도록 설계된 C89("ANSI C") 컴파일러다.
>
> LOG에는 지난 배포 이후의 변경이 있다.
>
> CPYRIGHT에는 lcc와 lcc에서 파생된 저작물을 사용, 복사, 수정, 배포할 수 있는 조건이 있다.
>
> doc/install.html은 배포판 전체와 설치 방법을 설명하는 HTML 파일이다.
>
> Chris Fraser / cwf@aya.yale.edu<br>
> David Hanson / drh@drhanson.net

`doc/install.html`은 원래 타깃만 다룬다.

## 라이선스

lcc의 라이선스는 [CPYRIGHT](CPYRIGHT)다. 그 파일과 함께라면 재배포할 수 있지만, lcc나 lcc가 가치의
큰 부분을 차지하는 제품은 팔 수 없다.
