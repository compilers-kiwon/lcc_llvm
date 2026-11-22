# lcc_llvm — LLVM·MLIR 백엔드를 단 lcc 4.2

[![CI](https://github.com/compilers-kiwon/lcc_llvm/actions/workflows/ci.yml/badge.svg)](https://github.com/compilers-kiwon/lcc_llvm/actions/workflows/ci.yml)

lcc는 Christopher W. Fraser와 David R. Hanson이 만든 재타깃 가능한 C89("ANSI C") 컴파일러다.
이 저장소는 [lcc 4.2](https://github.com/drh/lcc)에 백엔드 둘을 더했다. 하나는 **LLVM IR**을,
하나는 **MLIR**을 내며, 리눅스 다섯과 OS 없는 Cortex-M3까지 타깃은 여섯이다. 언어는 그대로 C89다.

- [원본 lcc 4.2 README(번역)](#원본-lcc-42-readme-번역)
- [lcc 4.x 코드 생성 인터페이스](https://drh.github.io/lcc/documents/interface4.pdf) — 백엔드가 받는 중간 코드
- [meta-lcc](meta-lcc/README.md) — lcc를 Yocto 교차 툴체인에 넣는 레이어
- [lcc-opt](lcc-opt/README.md) — lcc의 MLIR을 affine·linalg로 올리는 도구
- [CPYRIGHT](CPYRIGHT) — lcc의 라이선스

## 시작하기

**릴리스 파일로.** 빌드할 것 없이 x86_64 리눅스에서 바로 쓴다. 타깃·백엔드마다 압축 파일이 하나씩
[Releases](https://github.com/compilers-kiwon/lcc_llvm/releases)에 있다. llvm 드라이버는 clang 18이, mlir
드라이버는 LLVM 23이 있어야 한다([필요한 것](#필요한-것-ubuntu-2404-기준)). 압축에는 lcc만 들어 있고
[lcc-opt](#lcc-opt)는 [소스로 빌드한다](#lcc-opt-빌드). 첫 릴리스 v4.2-llvm.1의 mlir 압축은 기본값을 23으로 바꾸기 전에
만들어서 LLVM 21(`/usr/lib/llvm-21/bin`)을 쓴다.

```bash
curl -fsSLO https://github.com/compilers-kiwon/lcc_llvm/releases/download/v4.2-llvm.2/lcc-v4.2-llvm.2-llvm-x86_64-linux.tar.gz
tar -xzf lcc-v4.2-llvm.2-llvm-x86_64-linux.tar.gz
L=$PWD/lcc-v4.2-llvm.2-llvm-x86_64-linux
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
| 오브젝트 | llvm: clang 18 / mlir: LLVM 23의 `mlir-opt` → `mlir-translate` → clang |
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
| `mlir/*` | LLVM 23(`/usr/lib/llvm-23/bin`) |
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
| `LLVMBIN` | `/usr/lib/llvm-23/bin` | mlir 드라이버의 `mlir-opt`, `mlir-translate`, clang |
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
| `unit` | `tst/llvm/NAME.c`를 lcc로, `NAME-drv.c`를 clang이나 gcc로 컴파일해 링크하고 결과 확인 | 26 |
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
| `lcc-opt, T` | x86_64와 aarch64. [lcc-opt](#lcc-opt)를 빌드해 lit 시험, ASan·UBSan으로 빌드해 다시 lit 시험, `lcc-opt/kernels/check.sh`, 드라이버가 lcc-opt를 거치는 lcc로 위 시험 묶음(unit은 최적화한 드라이버로도) |
| riscv32 toolchain | Ubuntu에 없는 riscv32 툴체인을 빌드해 캐시에 둔다(처음 한 번 한 시간쯤) |
| Yocto layer | meta-lcc를 openembedded-core로 파싱하고 레시피가 무엇으로 풀리는지 확인 |

외부 시험 묶음은 정해 둔 커밋으로 CI에서 받는다([fetch-suites.sh](.github/scripts/fetch-suites.sh)).
타깃 하나의 설치·빌드는 [ci-target.sh](.github/scripts/ci-target.sh)가 하므로 로컬에서도 같은 명령으로 재현할 수 있다.
lcc-opt 작업의 단계는 [lcc-opt-check.sh](.github/scripts/lcc-opt-check.sh)의 하위 명령이고,
`lcc-opt-check.sh all T 작업디렉터리 시험묶음디렉터리`가 그 단계를 차례로 모두 돈다.

`v`로 시작하는 태그를 push하면 [release.yml](.github/workflows/release.yml)이 타깃·백엔드마다 lcc를 빌드하고
시험한 뒤 압축 파일(`lcc-TAG-BACKEND-TARGET.tar.gz`)로 GitHub Release에 올린다. lcc-opt는 압축에 넣지 않는다. 첫 릴리스는
[v4.2-llvm.1](https://github.com/compilers-kiwon/lcc_llvm/releases/tag/v4.2-llvm.1)이고, 기본 LLVM을 23으로 바꾸고 lcc-opt를
더한 [v4.2-llvm.2](https://github.com/compilers-kiwon/lcc_llvm/releases/tag/v4.2-llvm.2)가 그다음이다.

## Yocto

[meta-lcc](meta-lcc/README.md)는 lcc를 OpenEmbedded 빌드의 교차 툴체인에 `TARGET_PREFIX-lcc`로 넣는
레이어다. 레시피가 gcc처럼 lcc로 타깃 프로그램을 컴파일할 수 있다. MLIR 백엔드를 켜면
`TARGET_PREFIX-lcc-mlir`도 생긴다. 오브젝트는 Yocto의 `llc`(MLIR은 `mlir-opt`, `mlir-translate`를 거쳐)가
만들고 링크는 툴체인의 gcc가 한다. 리눅스 타깃 다섯을 지원하며, qemux86-64, qemuarm64, qemuriscv64,
qemuarm, qemuriscv32에서 두 드라이버 모두 위 시험 묶음이 전부 통과한다.

## lcc-opt

[lcc-opt](lcc-opt/README.md)는 mlir 백엔드가 낸 `.mlir`을 업스트림 MLIR의 최적화가 다룰 수 있는 모양으로 올리는 도구다. MLIR 23의
`mlir-opt`에 lcc-opt의 패스와 파이프라인을 더한 프로그램으로, 지역 변수를 SSA 값으로 바꾸고 호출을 인라인한 뒤 루프를 `scf.for`로,
고정 크기 전역 배열을 다루는 루프 묶음을 `affine.for`로, 다시 `linalg.matmul` 같은 linalg 연산으로 올린다. 그 결과에 업스트림의
affine 변환이나 linalg를 타일링·벡터화하는 transform 스크립트를 쓰고, LLVM 방언으로 내려 `mlir-translate`와 clang으로 오브젝트를
만든다. lcc와 따로 빌드하며, lcc의 코드는 들어 있지 않고 라이선스는
`Apache-2.0 WITH LLVM-exception`이다([LICENSE](lcc-opt/LICENSE)).

| 파이프라인 | 하는 일 |
| :- | :- |
| `lcc-prepare` | lcc가 지역 변수와 매개변수마다 만든 alloca를 SSA 값으로 바꾸고 호출을 인라인 |
| `lcc-to-scf` | 루프를 `scf.while`로, 셀 수 있는 루프는 `scf.for`로 |
| `lcc-to-affine` | 전역 배열을 `memref.global`로, 그 배열만 affine 첨자로 다루는 루프 묶음을 `affine.for`로 |
| `lcc-to-linalg` | 전역 배열을 C 배열의 모양으로 보고, 리덕션 둘레의 루프를 나누고, 루프 묶음을 linalg 연산으로 |
| `lcc-lower` | 위의 결과를, 업스트림 변환과 transform 스크립트 뒤에도, `mlir-translate`가 받는 LLVM 방언으로 |

### lcc-opt 빌드

[apt.llvm.org](https://apt.llvm.org)의 LLVM 23 패키지 `libmlir-23-dev`, `llvm-23-dev`, `mlir-23-tools`와 `cmake`,
`ninja-build`가 있으면 된다(Ubuntu 24.04 기준).

```bash
cmake -G Ninja -S lcc-opt -B build-lcc-opt
ninja -C build-lcc-opt                          # build-lcc-opt/bin/lcc-opt
ninja -C build-lcc-opt check-lcc-opt            # lit 시험
```

### lcc-opt 사용

lcc 드라이버는 lcc-opt를 부르지 않으므로 단계마다 명령을 쓴다. mlir 백엔드로 빌드한 lcc로 `.mlir`을 만들고, lcc-opt로 올리고
변환하고 내린 뒤, LLVM 23의 `mlir-translate`와 clang으로 오브젝트를 만든다. 아래는 [convfix.c](lcc-opt/kernels/convfix.c)를
linalg로 올려 타일링·벡터화하고, 같은 디렉터리의 `main.c`와 링크해 실행한다.

```bash
B=$PWD/build-mlir; mkdir -p $B
make BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c all headers
LCCDIR=$B $B/lcc -S lcc-opt/kernels/convfix.c                   # convfix.mlir
build-lcc-opt/bin/lcc-opt --lcc-to-linalg \
    --transform-preload-library=transform-library-paths=lcc-opt/kernels/tile-vector.mlir \
    --transform-interpreter --lcc-lower --emit-bytecode convfix.mlir -o convfix.mlirbc
/usr/lib/llvm-23/bin/mlir-translate --mlir-to-llvmir convfix.mlirbc -o convfix.ll
/usr/lib/llvm-23/bin/clang -O2 -c convfix.ll                    # convfix.o
gcc -O2 -DCONVFIX -o convfix lcc-opt/kernels/main.c convfix.o && ./convfix
```

`--lcc-to-linalg`가 linalg로 올리고, transform 스크립트 [tile-vector.mlir](lcc-opt/kernels/tile-vector.mlir)이 linalg 연산을
타일링·벡터화하며, `--lcc-lower`가 LLVM 방언으로 내린다. transform 스크립트는 `--transform-preload-library`로 준다. 페이로드 안에
두면 transform 모듈이 남아 `mlir-translate`가 읽지 못한다. MLIR 23의 텍스트 형식은 `x86_fp80`·`fp128` 상수와 `i64` switch의 일부
값을 다시 읽을 때 바꾸므로, 도구 사이에는 bytecode(`--emit-bytecode`)로 넘긴다. affine에서 멈추려면 `--lcc-to-linalg` 대신
`--lcc-to-affine`을 쓰고 그 뒤에 업스트림 affine 변환(`--affine-loop-tile=tile-sizes=8,8`,
`'--affine-super-vectorize=virtual-vector-size=16 vectorize-reductions=true'` 등)을 둔다. `--lcc-to-linalg=remarks=true`처럼
`remarks=true`를 주면 전역과 루프 묶음마다 바꾸었는지, 아니면 왜 남겼는지를 알려 준다.

### 완료 기준과 결과

[lcc-opt/kernels](lcc-opt/kernels)의 커널로 확인한다. `convfix.c`는 고정 크기 전역 배열의 1x1 int8 합성곱(곱-합, bias,
-128~127로 자르기)이고, 상수 가중치 표판 `convw.c`, float판 `convff.c`, 행렬 곱 `mm.c`, 3x3 합성곱 `conv3.c`가 더 있다.
[check.sh](lcc-opt/kernels/check.sh)가 IR의 모양을 보고, 커널마다 구성 여덟(lcc 드라이버 그대로, affine, affine 타일링, affine
벡터화, linalg, linalg 타일링, 타일링과 벡터화, 마스크 벡터화)으로 만든 LLVM IR을 clang `-O0`·`-O2`로 컴파일해 `gcc -O2`로
컴파일한 것과 실행 결과를 비교한다.

| 기준 | 결과 |
| :- | :- |
| ① `convfix.c`가 affine으로 오르고, 업스트림 `affine-loop-tile`과 `affine-super-vectorize`(`vectorize-reductions`)가 그 IR을 바꾼다 | `affine.for` 셋. 타일링 뒤 다섯, 벡터화 뒤 벡터 읽기와 `vector.reduction` |
| ② linalg로 올라 곱-합은 matmul, bias와 자르기는 원소별 연산이 된다 | `linalg.broadcast`(bias), `linalg.matmul`(곱-합), parallel 차원 둘인 `linalg.generic`(자르기) |
| ③ transform 스크립트로 linalg 연산을 타일링하고 벡터화한다 | [tile.mlir](lcc-opt/kernels/tile.mlir)(타일링), [tile-vector.mlir](lcc-opt/kernels/tile-vector.mlir)(타일링과 벡터화), [mask-vector.mlir](lcc-opt/kernels/mask-vector.mlir)(나누어떨어지지 않는 타일과 마스크 벡터화) |
| ④ ①·③의 결과가 x86_64·aarch64(qemu)에서 gcc와 비트까지 같다 | 두 타깃 모두 커널 다섯 × 구성 여덟 × `-O0`·`-O2`가 같다. 예외 둘은 [lcc-opt의 제약](#lcc-opt의-제약)에 |
| ⑤ lcc의 기본 LLVM을 23으로 바꾼 뒤 시험 묶음이 통과한다 | CI의 `llvm/T`·`mlir/T` 작업. 드라이버가 lcc-opt(`--lcc-to-linalg --lcc-lower`)를 거치는 lcc로도 x86_64·aarch64에서 lcc 19, unit 26, c-testsuite 144, torture 618, llvm-bench 26이 모두 통과한다 |

### 포팅 규약

lcc-opt가 올리는 커널은 다음을 지킨다.

- 배열은 파일 범위의 고정 크기 배열(`static` 포함)이고 크기는 상수다.
- 루프 변수는 `int`이고 루프는 `for (v = a; v < b; v++)`나 `v += 상수` 꼴이다. 루프 변수의 주소를 취하거나 본문에서 바꾸지 않는다.
- 루프 안에서 함수를 부르지 않는다. 인라인되는 `static` 함수는 된다.
- 한 배열을 다른 원소 형으로 읽지 않는다.

규약을 어긴 코드는 올라가지 않을 뿐 틀리게 컴파일되지 않는다. lcc-opt는 바꾼 뒤에도 같은 값이 됨을 상수와 값의 범위, affine 의존성
분석으로 보일 수 있을 때만 바꾸고, C의 부호 있는 넘침이 정의되지 않는다는 것에는 기대지 않는다.

### lcc-opt의 제약

- MLIR 23만 받는다. lcc-opt는 MLIR 23의 결함 몇 가지를 피해 간다. `func.func`를 인라인할 때 byval 인자를 복사하고 인라인한 함수의
  alloca를 진입 블록으로 옮기며(`lcc-byval-copy`, `lcc-hoist-allocas`), `llvm.func`의 루프를 올리고(`lcc-lift-llvm-func`),
  `scf.while`을 `scf.for`로 직접 바꾸고(`lcc-uplift-while`), 텍스트로 쓰면 값이 바뀌는 IR을 거부하고(`lcc-check-text`), LLVM
  IR로 내보낼 때의 블록 나누기를 정해진 순서로 먼저 하고(`lcc-distinct-successors`), 벡터 전송의 임시 버퍼가 루프 안에 생기지 않게
  한다(`lcc-lower`).
- 루프 묶음을 다른 연산 너머로 옮기는 업스트림 패스(`affine-loop-fusion` 등)는 쓰면 안 된다. lcc-opt는 루프 묶음 안만 보고 올리는데,
  MLIR 23의 `affine-loop-fusion`은 묶음을 같은 전역을 고치는 호출이나 `llvm.store` 너머로 옮겨 결과가 틀린다. 비슷한 업스트림
  이슈가 [llvm/llvm-project#211591](https://github.com/llvm/llvm-project/issues/211591),
  [llvm/llvm-project#211599](https://github.com/llvm/llvm-project/issues/211599)다.
- `affine-super-vectorize`는 `vectorize-reductions=true`로 쓴다. 없으면 `convfix.c`의 `o` 루프를 벡터화하면서 16바이트씩 떨어진
  `w[o][i]`를 이어진 원소처럼 읽어 결과가 틀린다. MLIR 23의 이 패스는 보폭이 1이 아닌 접근을 이어진 것으로 보는 결함이 있어 `mm.c`의
  `mm2`도 틀리게 벡터화하므로, check.sh는 `mm.c`의 affine 벡터화 구성을 돌리지 않는다.
- float 합을 벡터화하면 더하는 순서가 C와 달라져 마지막 자리가 다를 수 있다. `affine-super-vectorize`의 리덕션 벡터화가
  그렇고(`convff.c`에서 128값 중 24개, 상대 차이 3.1e-7 이하), check.sh는 이 구성의 `convff.c`는 비교하지 않고 다른 값의 수만
  알린다. linalg의 `transform.structured.vectorize`는 순서를 지킨다. `vectorize_children_and_apply_patterns`는 기본으로 순서를
  바꾸므로(128값 중 35개) `disable_multi_reduction_to_contract_patterns`를 준다.
- memref의 `linalg.conv_2d`는 MLIR 23의 transform으로 벡터화하지 못해 타일링만 한다.

자세한 것은 [lcc-opt/README.md](lcc-opt/README.md)에 있다.

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
| `lcc-opt/` | lcc의 MLIR을 affine·linalg로 올리는 도구(따로 빌드, Apache-2.0 WITH LLVM-exception) |
| `lcc-opt/kernels/` | lcc-opt의 완료 기준 커널, transform 스크립트, 검사 스크립트 |
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
