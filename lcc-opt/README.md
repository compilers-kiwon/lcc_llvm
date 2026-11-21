# lcc-opt

lcc의 MLIR 백엔드(`-target=mlir/T`)가 낸 `.mlir`을 업스트림 MLIR의 최적화(affine, linalg 등)가 다룰 수 있는
모양으로 올리는 도구다. MLIR 23으로 만든 `mlir-opt`이며, `MlirOptMain` 위에 업스트림의 방언·확장·패스를
모두 등록하고 lcc-opt의 패스와 파이프라인을 더한다. 지금은 루프를 `scf.for`까지 올리는 파이프라인 `lcc-to-scf`와
그 첫 부분인 `lcc-prepare`가 있다.

lcc와 따로 빌드하는 프로그램이다. lcc의 코드는 들어 있지 않고 라이선스도 다르다([라이선스](#라이선스)).

## 필요한 것 (Ubuntu 24.04 기준)

| 패키지 | 쓰는 곳 |
| :- | :- |
| `libmlir-23-dev` | MLIR 헤더, CMake 설정, `libMLIR.so` |
| `llvm-23-dev` | LLVM 헤더, CMake 설정, `libLLVM.so`. 시험에 쓰는 `lit`·`FileCheck`(`llvm-23-tools`)도 함께 설치된다 |
| `mlir-23-tools` | MLIR의 CMake 설정이 가리키는 `mlir-tblgen` 등. 비교용 `mlir-opt` |
| `cmake`, `ninja-build` | 빌드 |
| g++ 13 또는 clang 23 | C++17 컴파일러 |

LLVM 23 패키지는 [apt.llvm.org](https://apt.llvm.org)에 있다. 다른 곳에 설치한 MLIR을 쓰려면
`-DMLIR_DIR=…/lib/cmake/mlir`를 준다. MLIR 23이 아니거나 `libMLIR.so`·`libLLVM.so`가 없으면 CMake가 오류를 낸다.

## 빌드

저장소 최상위에서:

```bash
cmake -G Ninja -S lcc-opt -B build-lcc-opt
ninja -C build-lcc-opt
```

결과는 `build-lcc-opt/bin/lcc-opt`다. 패키지의 `mlir-opt`처럼 `libMLIR.so`와 `libLLVM.so`에 동적 링크한다.
정적 MLIR 라이브러리를 함께 링크하면 MLIR이 두 벌 들어가므로 쓰지 않는다. 우리 패스와 파이프라인은
`lib/`의 라이브러리 `LccOptPasses`로 따로 빌드해 도구에 링크한다.

## 시험

```bash
ninja -C build-lcc-opt check-lcc-opt
```

`test/*.mlir`을 `/usr/lib/llvm-23/bin`의 `lit`과 `FileCheck`로 돌린다. `prepare.mlir`과 `to-scf.mlir`의 입력은
lcc가 작은 C 함수에서 낸 `.mlir`이고, 만든 명령은 시험 파일의 주석에 있다. `rotate.mlir`과 `uplift.mlir`은 손으로
쓴 `scf.while`로 각 패스가 바꾸는 경우와 그대로 두어야 하는 경우를 본다.

## 사용

```bash
LCCDIR=$B $B/lcc -S foo.c                        # mlir/x86_64-linux로 빌드한 lcc: foo.mlir
build-lcc-opt/bin/lcc-opt --lcc-to-scf foo.mlir
```

`lcc-to-scf`의 출력을 LLVM IR로 내리려면 lcc 드라이버가 쓰는 패스 앞에 `--convert-scf-to-cf`와 `--convert-ub-to-llvm`을
더한다(`ub.poison`은 `lift-cf-to-scf`와 `lcc-rotate-do-while`이 만들 수 있다). 다른 도구에 넘길 때는 텍스트가 아니라
bytecode(`--emit-bytecode`)로 넘긴다. MLIR 23의 텍스트 출력은 `f80`·`f128` 상수를 십진수로 줄여 정밀도를 잃고,
`i64` `cf.switch`의 음수 case(예: `INT64_MIN`)를 다시 읽지 못하는 큰 양수로 쓴다. 패스 없이 읽고 쓰기만 해도 그렇다.

```bash
build-lcc-opt/bin/lcc-opt --lcc-to-scf --emit-bytecode foo.mlir -o foo.scf.mlirbc
mlir-opt --convert-scf-to-cf --convert-ub-to-llvm --convert-arith-to-llvm --convert-cf-to-llvm \
  --convert-func-to-llvm --reconcile-unrealized-casts --emit-bytecode foo.scf.mlirbc -o foo.mlirbc
mlir-translate --mlir-to-llvmir foo.mlirbc -o foo.ll
```

업스트림 패스는 `mlir-opt`과 같은 이름으로 쓴다(`--lift-cf-to-scf`, `--affine-loop-tile` 등). 목록은
`lcc-opt --help`에 있다. `mlir-opt`과 달리 업스트림의 시험용 방언과 패스(`--test-*` 등)는 없다.

## 파이프라인

| 이름 | 패스 | 하는 일 |
| :- | :- | :- |
| `lcc-prepare` | mem2reg, sroa, canonicalize, inline, mem2reg, sroa, canonicalize, cse | lcc가 지역 변수와 매개변수마다 만든 `llvm.alloca`를 SSA 값으로 바꾸고 호출을 인라인한다 |
| `lcc-to-scf` | lcc-prepare, lift-cf-to-scf, canonicalize, cse, lcc-rotate-do-while, canonicalize, lcc-uplift-while, canonicalize, loop-invariant-code-motion, canonicalize, cse | 루프를 `scf.while`로, 셀 수 있는 루프는 `scf.for`로 올린다 |

`lcc-prepare`의 출력은 `mlir-opt --mem2reg --sroa --canonicalize --inline --mem2reg --sroa --canonicalize --cse`와
같다. inline보다 mem2reg를 먼저 하는 것은, 인라인된 함수의 `llvm.alloca`가 호출이 있던 블록(흔히 루프 본문)에
들어가는데 mem2reg가 진입 블록 밖의 `llvm.alloca`는 올리지 않기 때문이다. 모든 alloca가 없어지지는 않는다.
`tst/*.c`에서는 배열·구조체 지역 변수 등의 alloca가 남았다.

`lcc-to-scf`에서 `lift-cf-to-scf`는 루프 본문을 `before` 영역의 `scf.if` 안에 두는데, 뒤의 canonicalize(MLIR 23의
`WhileMoveIfDown`)가 그것을 `after` 영역으로 내린다. MLIR 21에는 이 패턴이 없다. 업스트림 `lift-cf-to-scf`는
`func.func`만 올리므로, lcc가 가변 인자 함수 등에 쓰는 `llvm.func`의 루프는 `cf` 그대로 남는다(우리 두 패스는
`llvm.func` 안의 `scf.while`에도 동작한다).

## 패스

### `lcc-rotate-do-while`

lcc는 `for (e1; e2; e3) S`를 `e1; goto 시험; L: S; e3; 시험: if (e2) goto L`로 내리고, 첫 시험이 참임을 증명하면
(`for (i = 0; i < 16; i++)`처럼 상수끼리) 첫 점프를 뺀다. 그러면 do-while 모양이 되어 `lift-cf-to-scf`가
`before` 영역에 본문과 시험을 모두 두고 `after` 영역은 값을 넘기기만 하는 `scf.while`을 만든다. 이 패스는 본문을
`after` 영역으로 옮기고 `before` 영역에서는 유도 변수 자체를 시험한다. 다음을 모두 만족할 때만 바꾼다.

- `after` 영역은 블록 인자를 `scf.yield`로 넘기기만 한다(순서는 바뀌어도 된다).
- 조건이 `arith.cmpi slt (arith.addi iv, step), ub`이고, `iv`는 `before` 블록 인자, `step`은 루프 밖의 양의 정수 상수,
  `ub`는 루프 밖 값이다.
- 그 `addi`가 `iv` 자리로 넘어간다.
- 첫 시험 `init < ub`가 상수로 참이다. 그래야 do-while 루프에서 늘 한 번은 도는 본문이 바꾼 뒤에도 한 번은 돈다.

두 번째부터의 시험은 원래 루프가 비교하던 바로 그 값을 비교하므로 본문이 도는 횟수가 같다. `after` 영역이 넘기지
않는 조건 인자(마지막 반복의 값을 루프 뒤에서 쓰는 것)는, 루프 밖에서 정의됐거나 루프 밖 값만 쓰는 순수 연산으로
계산되면 그 연산을 루프 앞으로 옮겨 그 값으로 바꾸고, 그렇지 않으면 초기값이 `ub.poison`인 루프 운반 값을 하나
더한다. 본문이 적어도 한 번 돌므로 그 초기값은 쓰이지 않는다.

### `lcc-uplift-while`

`while` 모양 `scf.while`을 업스트림 `scf::upliftWhileToForLoop`(`populateUpliftWhileToForPatterns`의 패턴)으로
`scf.for`로 바꾼다. 업스트림 패턴과 두 가지가 다르다.

- `step`이 양의 정수 상수여야 한다. 업스트림 패턴은 루프 밖 값이면 받지만 `scf.for`는 `step`이 양수여야 하고,
  `step`이 0이거나 음수인 `while` 루프는 뜻이 다르다.
- 루프가 끝난 뒤의 유도 변수 값을 바르게 준다. 업스트림 패턴은 그 값을 `lb + ((ub - lb + step - 1) / step - 1) * step`,
  곧 마지막 반복의 값으로 바꾸지만 `scf.while`의 결과는 시험에서 거짓이 된 값(한 `step` 더 간 값)이고, 한 번도
  돌지 않으면 `lb`다. 예를 들어 `for (i = 0; i < 10; i += 3)` 뒤의 `i`는 12인데 업스트림 패턴은 9를 준다.
  이 패스는 그 결과가 쓰일 때 `lb`·`ub`·`step`이 상수면 상수로, 아니면 `lb`에서 시작해 `iv + step`을 받는 루프 운반
  값을 `scf.for`에 더해 그 결과로 바꾼다.

## 디렉터리

| 경로 | 내용 |
| :- | :- |
| `lcc-opt.cpp` | `main`: 업스트림 등록, 우리 등록, `MlirOptMain` |
| `include/lcc-opt/Passes.h`, `include/lcc-opt/Passes.td`, `lib/` | 우리 패스와 파이프라인(`LccOptPasses`). 패스 선언은 TableGen |
| `test/` | lit 시험 |

## 라이선스

lcc-opt는 Apache License v2.0 with LLVM Exceptions다([LICENSE](LICENSE)). 소스 파일마다
`SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`이 있다. 저장소의 나머지인 lcc는
[CPYRIGHT](../CPYRIGHT)를 따른다.
