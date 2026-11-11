# meta-lcc

LLVM 백엔드를 단 lcc를 교차 툴체인에 넣는 Yocto 레이어다. gcc-cross가
툴체인에 있는 것과 같은 방식이어서, 레시피가 `${TARGET_PREFIX}lcc`로
타깃용 C89 프로그램을 컴파일할 수 있다.

| 레시피 | 실행 위치 | 용도 |
| :- | :- | :- |
| `lcc-cross-${TARGET_ARCH}` | 빌드 호스트 | `x86_64-oe-linux-lcc` 등: lcc 드라이버, cpp, rcc, 헤더 |
| `lcc-runtime` | 타깃 | lcc가 모든 프로그램에 링크하는 `liblcc.a` |
| `lcc-hello` | 타깃 | 예제: lcc로 C 파일 하나를 컴파일해 패키지로 만듦 |

## 여기서 lcc가 동작하는 방식

`x86_64-oe-linux-lcc --sysroot=DIR file.c`는 다음을 차례로 실행한다.

1. lcc의 `cpp`: lcc 자체 헤더, 그다음 `DIR/usr/include`를 쓴다.
2. `rcc -target=llvm/x86_64-linux`: LLVM IR을 낸다.
3. `llvm-native`의 `llc`: 위치 독립(PIC) 오브젝트를 만든다.
4. `x86_64-oe-linux-gcc --sysroot=DIR`: `-llcc -lm`과 함께 링크한다.

드라이버는 `etc/oe-linux.c`다. cpp의 사전 정의 매크로와 rcc의 타깃은
그 타깃의 호스트 파일(`etc/llvm-x86_64-linux.c` 등)에서 가져오고, llc
옵션은 그 파일의 `LLCFLAGS`를 쓴다. `LLCFLAGS`는 clang이 그 타깃에
기본으로 주는 설정(RISC-V의 부동소수점 ABI, ARM의 VFP)이다. lcc의 다른
드라이버들은 오브젝트를 clang으로 만들기 때문이다.

| `TARGET_ARCH` | lcc 타깃 | `MACHINE` 예 |
| :- | :- | :- |
| `x86_64` | `x86_64-linux` | `qemux86-64` |
| `aarch64` | `aarch64-linux` | `qemuarm64` |
| `riscv64` | `riscv64-linux` | `qemuriscv64` |
| `arm`, hard float | `armhf-linux` | `qemuarm` |
| `riscv32` | `riscv32-linux` | `qemuriscv32` |

그 밖의 타깃에서는 레시피를 건너뛴다.

## 레시피에서 lcc 쓰기

```
DEPENDS = "lcc-cross-${TARGET_ARCH} lcc-runtime"

do_compile() {
    ${TARGET_PREFIX}lcc --sysroot=${STAGING_DIR_TARGET} ${LDFLAGS} \
        -o ${B}/prog ${S}/prog.c
}
```

`--sysroot`는 Yocto의 `${CC}`가 gcc에 붙여 주는 것과 같다.
`${LDFLAGS}`는 배포판의 링크 정책(GNU hash, relro, now)이며, lcc가
링크하는 gcc에 그대로 넘긴다. gcc식 `-Wl,...` 옵션도 통째로 넘어간다.
`${CFLAGS}`는 넘기지 않는다. gcc용 옵션이고, lcc는 `-Wf...`, `-Wp...`를
자기 옵션으로 읽기 때문이다.

툴체인의 lcc는 gcc-cross처럼 레시피의 sysroot에 있으며, SDK에는
들어가지 않는다.

## 라이선스

lcc의 라이선스는 `CPYRIGHT` 파일이다. 그 파일과 함께라면 재배포할 수
있지만, lcc나 lcc가 가치의 큰 부분을 차지하는 제품은 팔 수 없다.
`liblcc.a`는 모든 프로그램에 링크되는 lcc 코드라서 `lcc-runtime`에
`LICENSE_FLAGS = "commercial"`을 달았다. 빌드에서 이를 수락해야 한다.

```
LICENSE_FLAGS_ACCEPTED += "commercial_lcc-runtime"
```

## 제약

- lcc처럼 C89만 받는다. 일부 glibc 헤더는 C89를 넘는다.
  [저장소 README의 "제약"](../README.md#제약)을 보라.
- 32비트 ARM에서 lcc의 `time_t`는 32비트이고, Yocto는 나머지를 모두
  `_TIME_BITS=64`로 빌드한다. 그래서 lcc 코드와 다른 라이브러리 사이에
  `time_t`가 든 인터페이스는 맞지 않는다.
  [저장소 README의 "armhf의 off_t와 time_t"](../README.md#armhf의-off_t와-time_t)를 보라.
- LLVM 백엔드만 지원한다. MLIR 백엔드에는 MLIR 빌드가 필요한데
  openembedded-core에는 없다.
- `LAYERSERIES_COMPAT`에는 빌드해 본 시리즈인 `blacksail`만 적었다.

## 현황

openembedded-core `c2ea956`(blacksail, 2026-11-08), LLVM 23.1.3,
GCC 16.2로 빌드했고 [scripts/check-tests.sh](scripts/check-tests.sh)로
확인했다. 모든 머신에서 `lcc-hello`가 사용자 모드 qemu로 `gcd 6`을
출력하고, 모든 시험 묶음이 통과한다.

| `MACHINE` | lcc | unit | c-testsuite | torture | llvm-bench |
| :- | :- | :- | :- | :- | :- |
| `qemux86-64` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuarm64` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuriscv64` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuarm` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuriscv32` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |

Yocto 빌드에서만 드러난 것 세 가지:

| 증상 | 원인 |
| :- | :- |
| rcc가 첫 함수에서 죽음 | Yocto는 `-O2`로, lcc makefile은 `-g`로 빌드한다. lcc 할당기가 기호 안의 `long double`이 아니라 `double`에 맞춰 정렬했다 |
| make가 `yacc lburg/gram.y`를 실행하다 실패 | git checkout은 `gram.y`를 `gram.c`보다 새것으로 남길 수 있다. 레시피는 make를 내장 규칙 없이(`-r`) 실행한다 |
| `qemuarm` 프로그램이 `/usr/lib/ld-linux.so.3`을 찾음 | Yocto의 gcc는 `-mfloat-abi=hard`를 기본이 아니라 `${CC}`로 받는다. 드라이버가 `armhf-linux`용으로 링크할 때 넘긴다 |

## 사용법

[BUILDING.md](BUILDING.md)에 빈 컴퓨터부터 확인까지의 절차가 있다.
