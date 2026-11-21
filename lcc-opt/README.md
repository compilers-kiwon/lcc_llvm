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

`test/*.mlir`을 `/usr/lib/llvm-23/bin`의 `lit`과 `FileCheck`로 돌린다. `prepare.mlir`, `inline.mlir`,
`to-scf.mlir`의 입력은 lcc가 작은 C 함수에서 낸 `.mlir`이고, 만든 명령은 시험 파일의 주석에 있다. `rotate.mlir`,
`uplift.mlir`은 손으로 쓴 `scf.while`로 각 패스가 바꾸는 경우와 그대로 두어야 하는 경우를, `lower.mlir`은 `lcc-lower`가
`scf`·`ub`·arith·cf·func를 남기지 않는 것을, `text.mlir`은 텍스트 출력 검사와 bytecode가 값을 지키는 것을,
`llvm-func.mlir`은 `llvm.func`의 루프(가변 인자 함수, 무한 루프)를 올리는 것을, `memcpy.mlir`·`split.mlir`은
구조체 복사와 멤버를 SSA 값으로 올리는 것을 본다.

## 사용

```bash
LCCDIR=$B $B/lcc -S foo.c                        # mlir/x86_64-linux로 빌드한 lcc: foo.mlir
build-lcc-opt/bin/lcc-opt --lcc-to-scf foo.mlir
```

`lcc-to-scf`의 출력은 파이프라인 `lcc-lower`로 LLVM 방언까지 내린다. lcc 드라이버의 `mlir-opt`는 arith·cf·func만
내리지만 `lcc-to-scf`의 출력에는 `scf`와, `lift-cf-to-scf`가 만든 `ub.poison`이 남을 수 있다. 다른 도구에 넘길 때는
텍스트가 아니라 bytecode(`--emit-bytecode`)로 넘긴다.

MLIR 23의 텍스트 형식은 두 가지 값을 다시 읽을 때 바꾼다. parser가 십진 실수를 double로 읽으므로 double이 아닌
`f80`·`f128` 상수(예: `0.1L`)가 double 값이 되고, `cf.switch`의 printer가 case를 부호 없이 쓰므로 `i64`의 음수
case(예: `INT64_MIN`)는 다시 읽지 못한다(generic 형식은 부호대로 쓴다). 패스 없이 읽고 쓰기만 해도 그렇다. 그래서
lcc-opt는 텍스트로 쓸 때(`--emit-bytecode`가 없을 때) 파이프라인 끝에 `lcc-check-text`를 돌려, 그런 값이 있으면
연산마다 오류를 내고 아무것도 쓰지 않는다. `--lcc-allow-lossy-text`를 주면 경고만 내고 쓴다. 시험 묶음의 lcc 출력
854개 파일에서 이 검사가 잡는 파일은 텍스트로 쓰고 다시 읽으면 달라지는 파일과 같다(lcc 출력 그대로 10개,
`lcc-to-scf` 뒤 7개).

```bash
build-lcc-opt/bin/lcc-opt --lcc-to-scf --lcc-lower --emit-bytecode foo.mlir -o foo.mlirbc
mlir-translate --mlir-to-llvmir foo.mlirbc -o foo.ll
```

업스트림 패스는 `mlir-opt`과 같은 이름으로 쓴다(`--lift-cf-to-scf`, `--affine-loop-tile` 등). 목록은
`lcc-opt --help`에 있다. `mlir-opt`과 달리 업스트림의 시험용 방언과 패스(`--test-*` 등)는 없다.

## 파이프라인

| 이름 | 패스 | 하는 일 |
| :- | :- | :- |
| `lcc-prepare` | lcc-memcpy-intrinsic, mem2reg, sroa, mem2reg, canonicalize, lcc-byval-copy, inline, lcc-hoist-allocas, lcc-split-aggregates, mem2reg, sroa, mem2reg, canonicalize, cse | lcc가 지역 변수와 매개변수마다 만든 `llvm.alloca`를 SSA 값으로 바꾸고 호출을 인라인한다 |
| `lcc-to-scf` | lcc-prepare, lift-cf-to-scf, lcc-lift-llvm-func, canonicalize, cse, lcc-rotate-do-while, canonicalize, lcc-uplift-while, canonicalize, loop-invariant-code-motion, canonicalize, cse | 루프를 `scf.while`로, 셀 수 있는 루프는 `scf.for`로 올린다 |
| `lcc-lower` | convert-scf-to-cf, convert-ub-to-llvm, convert-arith-to-llvm, convert-cf-to-llvm, convert-func-to-llvm, reconcile-unrealized-casts | `lcc-to-scf`의 출력을 `mlir-translate`가 받는 LLVM 방언으로 내린다 |

MLIR의 인라이너는 인자 처리를 callee의 방언에, 인라인한 블록의 처리를 호출 연산의 방언에 맡긴다. LLVM 방언은
`llvm.func`·`llvm.call`이면 `llvm.byval` 인자를 caller 쪽에 복사하고 callee의 상수 크기 `llvm.alloca`를 caller의
진입 블록으로 옮기지만, lcc가 대부분의 함수에 쓰는 `func.func`·`func.call`에는 func 방언이 둘 다 하지 않는다.
그대로 인라인하면 구조체를 값으로 받는 함수가 caller의 구조체를 고치고, 루프 안에서 인라인된 함수의 지역 배열은
반복마다 스택을 더 쓴다(256바이트 지역 배열을 쓰는 함수를 1000만 번 부르는 루프가 세그폴트를 냈다). 그래서
inline 앞뒤에 `lcc-byval-copy`와 `lcc-hoist-allocas`를 둔다([패스](#패스)).

lcc는 구조체 복사를 `llvm.call_intrinsic "llvm.memcpy…"`로, 구조체·배열 지역 변수를 `!llvm.array<N x i8>`로 낸다.
sroa·mem2reg는 앞의 것을 주소가 빠져나가는 호출로 보고, 뒤의 것은 원소인 바이트를 가로지르는 load·store 때문에
나누지 못한다. 그래서 `lcc-memcpy-intrinsic`과 `lcc-split-aggregates`를 두고, sroa가 나눈 alloca를 올리도록 sroa
뒤마다 mem2reg를 한 번 더 둔다. 시험 묶음의 lcc 출력 854개 파일에서 `lcc-prepare` 뒤 남는 alloca는 2143개에서
1247개로 줄었다. 남은 것은 같은 바이트를 다른 형으로 읽고 쓰는 것 475개(레지스터로 받은 구조체 인자를 `i64` 등으로
통째 저장하고 필드로 읽는 ABI 강제 변환, 공용체), 주소가 빠져나가는 것 439개(호출 인자, 포인터 비교에 쓰는
`ptrtoint`, 저장, 블록 인자 등), 가변 첨자 배열 150개, 구조체형 값을 읽고 쓰는 ABI 코드 97개, `va_list` 72개,
volatile 6개, 그 밖 8개(64자리가 넘는 큰 배열 등)다. 앞의 것은 정수 조각으로 나누면 올릴 수 있지만 아직 하지 않는다.

`lcc-to-scf`에서 `lift-cf-to-scf`는 루프 본문을 `before` 영역의 `scf.if` 안에 두는데, 뒤의 canonicalize(MLIR 23의
`WhileMoveIfDown`)가 그것을 `after` 영역으로 내린다. MLIR 21에는 이 패턴이 없다. 업스트림 `lift-cf-to-scf`는
`func.func`만 올리므로, lcc가 가변 인자 함수 등에 쓰는 `llvm.func`는 `lcc-lift-llvm-func`가 올린다.

## 패스

### `lcc-memcpy-intrinsic`

lcc의 `llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"`(32비트 타깃은 `.i32`)를, volatile 인자가 상수이면 같은 피연산자·
volatile·정렬(`arg_attrs`)의 `llvm.intr.memcpy`로 바꾼다. lcc가 `call_intrinsic`을 쓴 것은 MLIR 21의
`llvm.intr.memcpy`에 정렬을 줄 수 없었기 때문이다.

### `lcc-split-aggregates`

`llvm.alloca 1 x !llvm.array<N x i8>`의 쓰임이 모두 상수 첨자 `getelementptr … i8`을 거친 정수·실수·포인터의
non-volatile load·store이거나, 다른 포인터와의 상수 길이 non-volatile `llvm.intr.memcpy`이면, 멤버(오프셋과 형)마다
스칼라 alloca로 나눈다. load·store끼리는 같은 오프셋·같은 형이 아니면 겹치지 않아야 하고, memcpy가 멤버를 일부만
덮으면 안 되며, load·store가 하나는 있어야 한다. memcpy는 범위 안의 자리마다 load와 store로 바뀌고, 멤버가 덮지 않는
바이트(패딩, 쓰지 않는 멤버)는 8바이트 이하 정수 채움 자리가 되어 모든 바이트가 그대로 복사된다. 자리는 64개까지.
나눈 alloca는 mem2reg가 올린다.

### `lcc-byval-copy`

인라이너 앞에서, 본문이 있는 `func.func`의 `llvm.byval` 인자마다 진입 블록에 그 타입의 `llvm.alloca`(정렬은 타입의
ABI 정렬과 인자의 `llvm.align` 중 큰 것)와 인자에서의 `llvm.intr.memcpy`를 두고 본문이 복사본을 쓰게 한다. byval
인자는 이미 callee의 복사본이므로 뜻은 같고, 인라인되면 이 복사가 호출마다 caller 쪽에서 일어난다. 인라인되지 않은
함수에는 복사가 하나 더 남는다. 인자를 쓰는 곳이 그런 memcpy 하나뿐이면 다시 복사하지 않는다.

### `lcc-hoist-allocas`

인라이너 뒤에서, `func.func`·`llvm.func` 본문의 진입 블록이 아닌 블록에 있는 상수 개수 `llvm.alloca`를 진입 블록
처음으로 옮긴다. lcc는 alloca를 모두 진입 블록에 두므로(시험 묶음의 lcc 출력 854개 파일, alloca 11775개 모두),
인라이너 뒤 진입 블록 밖의 alloca는 인라인된 함수의 것이다. 옮긴 alloca는 뒤의 mem2reg·sroa가 올릴 수 있다. 안쪽
영역의 alloca는 다른 할당 범위일 수 있어 두고, LLVM 방언과 달리 lifetime 표시를 넣지 않으므로 옮긴 alloca끼리 스택
자리를 나눠 쓰지 않는다.

### `lcc-lift-llvm-func`

업스트림 `lift-cf-to-scf`는 `func.func`만 보고, 출구 없는 무한 루프 뒤에 둘 종결로 `func.return`을 만든다. lcc는 가변
인자 함수, 원형 없이 먼저 불린 함수, `-Wf-llvmfunc`이면 모든 함수를 `llvm.func`로 낸다. 이 패스는 같은 변환
(`ControlFlowToSCFTransformation`)을 분기가 모두 `cf.br`·`cf.cond_br`·`cf.switch`(lcc가 쓰는 것)인 `llvm.func` 본문에
적용하고, 무한 루프 뒤에는 `llvm.return`(값을 돌려주는 함수면 poison 값)을 둔다. 출구 종류가 하나로 남아야 `cf`가
남지 않는다. 다른 분기(`llvm.br` 등)가 있는 함수는 그대로 둔다.

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
계산되면 그 연산을 루프 앞으로 옮겨 그 값으로 바꾸고, 그렇지 않으면 초기값이 0(`arith.constant`, 포인터 등은
`llvm.mlir.zero`)인 루프 운반 값을 하나 더한다. 본문이 적어도 한 번 돌므로 그 초기값은 쓰이지 않는다. 0을 만들 수
없는 형(텐서 등)이면 바꾸지 않는다.

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
| `lcc-opt.cpp` | `main`: 업스트림 등록, 우리 등록, 텍스트로 쓸 때 `lcc-check-text`를 붙여 `MlirOptMain` |
| `include/lcc-opt/Passes.h`, `include/lcc-opt/Passes.td`, `lib/` | 우리 패스와 파이프라인(`LccOptPasses`). 패스 선언은 TableGen |
| `test/` | lit 시험 |

## 라이선스

lcc-opt는 Apache License v2.0 with LLVM Exceptions다([LICENSE](LICENSE)). 소스 파일마다
`SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`이 있다. 저장소의 나머지인 lcc는
[CPYRIGHT](../CPYRIGHT)를 따른다.
