# lcc-opt

lcc의 MLIR 백엔드(`-target=mlir/T`)가 낸 `.mlir`을 업스트림 MLIR의 최적화(affine, linalg 등)가 다룰 수 있는
모양으로 올리는 도구다. MLIR 23으로 만든 `mlir-opt`이며, `MlirOptMain` 위에 업스트림의 방언·확장·패스를
모두 등록하고 lcc-opt의 패스와 파이프라인을 더한다. 지금은 루프를 `scf.for`까지 올리는 파이프라인 `lcc-to-scf`와
그 첫 부분인 `lcc-prepare`, 그 뒤에 전역 배열을 `memref.global`로, 그 배열만 다루는 루프 묶음을 `affine.for`로 올리는
`lcc-to-affine`이 있다.

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
`scf`·`ub`·arith·cf·func와 affine·memref·vector(업스트림 타일링·벡터화의 결과)를 남기지 않는 것을, `text.mlir`은
텍스트 출력 검사와 bytecode가 값을 지키는 것을, `llvm-func.mlir`은 `llvm.func`의 루프(가변 인자 함수, 무한 루프)를
올리는 것을, `memcpy.mlir`·`split.mlir`은 구조체 복사와 멤버를 SSA 값으로 올리는 것을 본다. `index-loops.mlir`,
`globals-to-memref.mlir`, `scf-to-affine.mlir`은 lcc 출력 모양으로 손으로 쓴 입력으로 각 패스가 바꾸는 경우와 남기는
경우를 보고, 남기는 이유는 `remarks=true`가 내는 remark로 본다. `distinct-successors.mlir`은 반복되는 후속 블록을 그 순서대로
나누는 것을 본다. `to-affine.mlir`의 입력은 lcc가 `convfix.c`(x86_64와
32비트 armhf)와 상수 가중치 표를 쓰는 변형에서 낸 `.mlir`이다.

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

`lcc-to-affine`은 고정 크기 파일 범위 배열을 상수 경계의 `for` 루프로 다루는 커널(포팅 규약)을 affine으로 올린다.
업스트림 affine 변환을 그 뒤에 두고, 내릴 때는 역시 `lcc-lower`를 쓴다.

```bash
build-lcc-opt/bin/lcc-opt --lcc-to-affine foo.mlir           # affine.for, affine.load, affine.store
build-lcc-opt/bin/lcc-opt --lcc-to-affine --affine-loop-tile=tile-sizes=8,8 \
    '--affine-super-vectorize=virtual-vector-size=16 vectorize-reductions=true' \
    --lcc-lower --emit-bytecode foo.mlir -o foo.mlirbc
```

`--lcc-to-affine=remarks=true`를 주면 전역마다, 루프마다 바꾸었는지, 아니면 왜 남겼는지를 remark로 알린다
(`--mlir-print-op-on-diagnostic=false`를 함께 주면 짧다). 업스트림 `affine-super-vectorize`에는 MLIR 23에서 틀린 코드를
내거나 죽는 경우가 있다([업스트림 affine 변환](#업스트림-affine-변환)).

## 파이프라인

| 이름 | 패스 | 하는 일 |
| :- | :- | :- |
| `lcc-prepare` | lcc-memcpy-intrinsic, mem2reg, sroa, mem2reg, canonicalize, lcc-byval-copy, inline, lcc-hoist-allocas, lcc-split-aggregates, mem2reg, sroa, mem2reg, canonicalize, cse | lcc가 지역 변수와 매개변수마다 만든 `llvm.alloca`를 SSA 값으로 바꾸고 호출을 인라인한다 |
| `lcc-to-scf` | lcc-prepare, lift-cf-to-scf, lcc-lift-llvm-func, canonicalize, cse, lcc-rotate-do-while, canonicalize, lcc-uplift-while, canonicalize, loop-invariant-code-motion, canonicalize, cse | 루프를 `scf.while`로, 셀 수 있는 루프는 `scf.for`로 올린다 |
| `lcc-to-affine` | lcc-to-scf, lcc-index-loops, lcc-globals-to-memref, canonicalize, cse, lcc-scf-to-affine, canonicalize, cse | 셀 수 있는 루프를 index `scf.for`로, 전역 배열을 1차원 `memref.global`로, 그 배열만 affine 첨자로 다루는 루프 묶음을 `affine.for`로 올린다. 옵션 `remarks` |
| `lcc-lower` | convert-vector-to-scf, convert-vector-to-llvm, expand-strided-metadata, lower-affine, convert-scf-to-cf, convert-ub-to-llvm, finalize-memref-to-llvm, convert-arith-to-llvm, convert-cf-to-llvm, convert-func-to-llvm, reconcile-unrealized-casts, lcc-distinct-successors | `lcc-to-scf`·`lcc-to-affine`의 출력을, 업스트림 affine 변환 뒤에도, `mlir-translate`가 받는 LLVM 방언으로 내린다 |

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

LLVM IR의 phi는 같은 선행 블록에서 오는 서로 다른 값을 구별하지 못하므로, 한 분기가 같은 블록에 블록 인자를 넘기며 여러
번 가면(switch의 여러 case가 같은 블록에 다른 값을 넘기는 등) `mlir-translate`가 LLVM IR로 내보내기 전에 두 번째부터
사이 블록을 넣어 나눈다. MLIR 23의 이 코드(`LegalizeForExport.cpp`의 `ensureDistinctSuccessors`)는 그런 목적지를 블록
포인터를 키로 한 해시 맵(`SmallDenseMap`)에 모아 그 순서대로 사이 블록을 만들므로, 한 분기에 그런 목적지가 둘 이상이면
LLVM IR의 블록 순서와 phi 피연산자 순서가 실행마다 달라진다(같은 입력 파일로도 재현되고 뜻은 같다). `lcc-lower`는 끝에
`lcc-distinct-successors`를 두어 같은 나누기를 후속 블록 순서대로 먼저 하므로 `mlir-translate`가 나눌 것이 남지 않는다.
`lcc-prepare`만 돌리고 내리면 mem2reg가 만든 블록 인자 때문에 이런 switch가 생긴다. `convert-vector-to-llvm`은 앞에
둔다. 이 패스는 변환 전에 모듈 전체를 탐욕적으로 다시 쓰는데, `convert-scf-to-cf` 뒤에 두면 블록을 합쳐 이런 switch를
만든다. 앞에 두면 벡터가 없는 코드는 이 패스들이 없던 `lcc-lower`와 같은 LLVM IR이 된다(시험 묶음 5445개 파일).

`lcc-to-affine`의 세 패스는 포팅 규약을 어긴 코드를 틀리게 컴파일하지 않도록, 바꾼 뒤에도 같은 값이 됨을 상수와 값의
범위로 보일 수 있을 때만 바꾸고 C의 부호 있는 넘침이 정의되지 않는다는 것에는 기대지 않는다. affine 분석은 다른
memref끼리 겹치지 않는다고 보고 다른 메모리 연산은 보지 않으므로, 루프 묶음 안에 호출이나 포인터를 거친 접근이 하나라도
있으면 올리지 않는다. 시험 묶음의 lcc 출력(x86_64, 830개 파일)에서는 전역 157개(실수 표 포함)가 `memref.global`이
되고 루프 묶음 49개(루프 79개)와 다른 루프 안의 묶음 7개가 올라간다. 시험 묶음은 규약대로 쓴 코드가 아니어서, 남는
루프는 대부분 포인터 매개변수를 거친 접근(`llvm.getelementptr`), `llvm.func`(가변 인자 함수 등), 호출 때문이다.

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
- 조건이 `before` 블록 인자 `iv` 자리로 넘어가는 값과 루프 밖 값 `ub` 사이의 `arith.cmpi`다. 술어와 `iv`의 갱신식은
  가리지 않고, `ub`가 왼쪽이어도 된다.
- 첫 시험(`iv`의 초기값과 `ub`의 비교)이 상수로 참이다. 그래야 do-while 루프에서 늘 한 번은 도는 본문이 바꾼
  뒤에도 한 번은 돈다.

두 번째부터의 시험은 원래 루프가 비교하던 바로 그 값끼리 비교하므로, 술어와 갱신식이 무엇이든 본문이 도는 횟수가
같다. `after` 영역이 넘기지 않는 조건 인자(마지막 반복의 값을 루프 뒤에서 쓰는 것)는, 루프 밖에서 정의됐거나 루프 밖
값만 쓰는 순수 연산으로 계산되면 그 연산을 루프 앞으로 옮겨 그 값으로 바꾸고, 그렇지 않으면 초기값이
0(`arith.constant`, 포인터 등은 `llvm.mlir.zero`)인 루프 운반 값을 하나 더한다. 본문이 적어도 한 번 돌므로 그 초기값은
쓰이지 않는다. 0을 만들 수 없는 형(텐서 등)이면 바꾸지 않는다.

### `lcc-uplift-while`

`before` 영역이 `before` 블록 인자 `iv`와 루프 밖 값의 `arith.cmpi` 하나와 모든 인자를 넘기는 `scf.condition`이고,
`after` 영역이 `iv`에 `iv + s`나 `iv - s`(`s`는 상수)를 넘기는 `scf.while`을 `scf.for`로 바꾼다. `scf.for`는 마지막
유도 변수가 형에 들어가지 않는 루프를 정의하지 않지만 `scf.while`은 그냥 감싸므로, `iv`가 감싸지 않는다는 것을 상수로
보일 수 있을 때만 바꾼다. C의 부호 있는 넘침이 정의되지 않는다는 것에는 기대지 않는다.

| 꼴 | 조건 | 결과 |
| :- | :- | :- |
| `iv < ub` (slt, ult), 증가 | `s = 1`, 또는 `ub`가 상수이고 `ub + s - 1`이 형에 들어감 | `scf.for [unsigned] iv = init to ub step s` |
| `iv <= ub` (sle, ule), 증가 | `ub`가 상수이고 `ub + s`가 형에 들어감 | `iv < ub + 1`로 위와 같이 |
| `iv != ub` (ne), 증가 | `s = 1`이고 `init <= ub`(상수로, 부호 있게나 없게) 또는 `ub`가 형의 최댓값 | `iv < ub`로 위와 같이 |
| `iv > lb` (sgt, ugt), 감소 | `s = 1`, 또는 `lb`가 상수이고 `lb - s + 1`이 감싸지 않음 | `scf.for unsigned k = 0 to n step 1`, `iv = init - k * s` |
| `iv >= lb` (sge, uge), 감소 | `lb`가 상수이고 `lb - s`가 감싸지 않음 | `iv > lb - 1`로 위와 같이 |
| `iv != lb` (ne), 감소 | `s = 1`이고 `init >= lb`(상수로) 또는 `lb`가 최솟값(`u != 0` 등) | `iv > lb`로 위와 같이 |

감소하는 루프의 횟수 `n`은 `init > lb`면 `(init - lb - 1) / s + 1`, 아니면 0이다. 루프 뒤의 유도 변수(`scf.while`의
결과)는 증가하면 `init + n * s`(`n`은 `ub > init`면 `(ub - init - 1) / s + 1`, 아니면 0), 감소하면 `init - n * s`이고,
`iv`와 같은 폭의 부호 없는 산술로 계산해도 결과가 형에 들어가므로 정확하다. 업스트림
`populateUpliftWhileToForPatterns`는 `slt`·`sgt` 증가만 다루고, 실행 시간에 0이거나 음수일 수 있는 step도 받으며, 루프
뒤의 유도 변수를 한 step 모자라게(한 번도 돌지 않으면 `lb`가 아니게) 주어서 쓰지 않는다. 예를 들어
`for (i = 0; i < 10; i += 3)` 뒤의 `i`는 12인데 업스트림 패턴은 9를 준다. 전 판에서 받던 "step이 1보다 크고 경계가
변수인 증가 루프"(`for (i = 0; i < n; i += 4)`)는 `i`가 `INT_MAX`를 넘어 감쌀 수 있으므로 이제 바꾸지 않는다.

### `lcc-index-loops`

정수형 `scf.for`(lcc의 C 루프 변수 형, 대개 `i32`)를 `index` 루프로 바꾼다. `scf.for`에서 유도 변수는 `lb + j * s`를
정확히 계산한 값이고 `lb + n * s`(n은 횟수)가 형에 들어가야 하므로, 경계와 step을 부호 확장해도(unsigned 루프는 0 확장)
같은 값으로 같은 횟수를 돈다. 그래서 index가 iN의 모든 값을 담으면 바꾸고, `scf.for unsigned`는 부호 있는 index
루프가 되므로 경계가 부호 있는 수로도 음이 아니어야 한다: N이 index 폭보다 작으면 늘 그렇고, 같으면(`i64`) 경계의
범위가 음이 아니고 `ub + step - 1`이 넘치지 않음을 보일 때만 바꾼다. 본문은 index 유도 변수를 iN으로 되돌린 값을 쓰고,
유도 변수의 `extsi`(unsigned 루프는 `extui`)와 index 캐스트는 index 유도 변수의 캐스트로 바꾼다. 유도 변수의 값이 iN에
들어가므로 같은 값이다. MLIR의 index 폭은 데이터 배치 명세(DLTI)가 없으면 64비트라서, 32비트 타깃에서도 유도 변수는
64비트 산술이 된다.

### `lcc-globals-to-memref`

lcc는 전역 배열을 `!llvm.array<N x i8>`(0이 이어지는 초기값은 바이트 배열과 0의 packed 구조체, 긴 것은 256개씩 중첩)로
내고, 원소는 `llvm.mlir.addressof`와 바이트 오프셋의 `llvm.getelementptr`, `llvm.load`·`llvm.store`로 읽고 쓴다. 이
패스는 그런 전역을 `memref<(N / sizeof(T)) x T>`의 1차원 `memref.global`로, 그 load·store를 원소 첨자의 `memref.load`·
`memref.store`로 바꾼다. T는 그 전역의 모든 load·store의 형이다. 다음을 모두 만족하는 전역만 바꾼다.

- 위의 바이트 배열이다. 스칼라 변수(lcc는 제 형으로 낸다), 포인터 배열, 크기가 0인 불완전 배열 선언이 아니다.
- 링크가 external이나 internal이다(lcc의 문자열 상수와 지역 배열 초기값은 private이라 바꾸지 않는다). 주소 공간 0,
  section·comdat·thread_local·다른 가시성이 없다.
- 이름은 함수 본문의 `llvm.mlir.addressof`만 쓴다. 다른 전역의 초기값(과 제 초기값)이 주소를 쓰지 않는다.
- load·store가 있고 모두 같은 형 T이며(타입 퍼닝이 아님), T는 8·16·32·64비트 정수나 `f32`·`f64`이고, N이
  sizeof(T)의 배수다.
- 초기값이 `llvm.mlir.zero`(→ `dense<0>`), 주소 조각이 없는 바이트 덩어리(→ 리틀 엔디언으로 푼 T의 `dense`, 음수
  포함), 또는 없다(선언 → 초기값 없는 `memref.global`). `f32`·`f64` 표는 바이트를 비트 그대로 풀어 −0.0, NaN의
  페이로드와 부호, 무한대, 비정규 수가 남는다. NaN·무한대는 텍스트에서 16진으로, 다른 값은 다시 읽어도 같은 십진수로
  찍혀 `lcc-check-text`도 통과한다. 빅 엔디언의 바이트 초기값은 바꾸지 않는다.

external은 public, internal은 private가 되고(memref를 내리면 LLVM private 링크), constant와 정렬은 그대로다. load·store는
volatile·원자 연산이 아니고 sizeof(T)보다 작은 정렬 같은 다른 속성이 없으며, 주소가 전역에 바이트 오프셋을 더한 것이고
그 합이 LinearAnalysis로 `c0 + Σ c_k·x_k`(c0과 모든 c_k가 sizeof(T)의 배수)임을 포인터의 인덱스 폭에서 정확히 보일 때
memref 접근이 된다. 첨자는 index 산술의 `c0 / sizeof(T) + Σ (c_k / sizeof(T))·x_k`다. 32비트 타깃에서 lcc는 첨자에 원소 크기를 `i32`로
곱한 뒤 확장하므로(넘칠 수 있음) 루프 밖 `a[k]`처럼 범위를 모르는 접근은 바꾸지 않는다. 그 밖의 쓰임(volatile 접근, 호출
인자, 저장된 주소, 넘칠 수 있는 오프셋 등)은 memref에서 만든 포인터(`memref.extract_aligned_pointer_as_index`,
`arith.index_cast`, `llvm.inttoptr`)를 쓴다. memref 접근을 내리면 `getelementptr inbounds|nuw`가 되므로 배열 밖 접근은
LLVM에서도 정의되지 않는다(C에서도 정의되지 않는다).

### `lcc-scf-to-affine`

index `scf.for` 루프 묶음을 `affine.for`로, 그 안의 memref 접근을 `affine.load`·`affine.store`로 올린다. 바깥 루프부터
보며, 묶음 전체가 다음을 만족하는 첫 루프를 안쪽 루프와 함께 올린다. 아니면 그 안의 루프를 따로 본다.

- 모든 루프가 부호 있는 index `scf.for`이고 step은 양의 상수, 경계는 묶음의 바깥 루프 유도 변수(차원)와 심볼의
  affine 식이다. 심볼은 상수, 묶음 밖에서 정의된 유효한 affine 심볼, 또는 함수 최상위에서 정의된 정수(정의 바로 뒤에
  `arith.index_cast`를 두어 쓴다)다. 다른 루프 안에서 정의된 값은 MLIR에서 심볼이 될 수 없다.
- 묶음에는 부작용 없는 arith 연산, 상수, `ub.poison`, `scf.if`, 묶음의 안쪽 루프, 그리고 `memref.get_global`에서 온
  1차원 memref의 `memref.load`·`memref.store`만 있다. 첨자는 경계처럼 affine이고, 루프 경계로 계산한 첨자의 범위가
  memref 안이어야 한다.
- affine 영역인 `func.func` 안이다(`llvm.func`는 아님).

호출, `llvm.load`·`llvm.store`, `llvm.alloca`, 포인터 연산 등이 하나라도 있으면 그 묶음은 올리지 않는다. 첨자 식은 직접
affine 맵으로 만든다. 업스트림 `affine-raise-from-memref`는 index의 `arith.addi`·`arith.muli`만 affine으로 알아보고
lcc 주소 계산의 `arith.shli`, 확장, index 캐스트는 알아보지 못하며, 올리지 못한 접근은 affine 루프 안에 `memref.load`·
`memref.store`로 남기는데 affine 분석은 그것을 보지 않는다.

묶음 사이를 옮기는 업스트림 패스(`affine-loop-fusion` 등)는 묶음 밖의 호출과 포인터 접근을 보지 않아 안전하지 않다. 예를
들어 MLIR 23의 `affine-loop-fusion`은 producer 모드에서도 루프 묶음을 `func.call`이나, 주소가 빠져나간 전역으로의
`llvm.store` 너머로 옮겨, 그 호출이나 저장이 같은 전역을 고치면 결과가 틀린다.

### `lcc-distinct-successors`

`mlir-translate`가 내보내기 전에 하는 나누기를 결정적인 순서로 먼저 한다. 종결 연산마다 후속 블록을 차례로 보며, 블록
인자가 있는 같은 블록이 두 번째부터 나오는 자리를 새 블록으로 돌린다. 새 블록은 영역 끝에 붙고, 같은 인자를 받아 원래
블록으로 `llvm.br`한다. 인자가 없는 블록은 phi가 필요 없어 반복되어도 둔다. 만드는 블록은 MLIR의
`ensureDistinctSuccessors`와 같고 순서만 정해져 있으므로, 그런 목적지가 하나뿐인 분기에서는 전과 같은 LLVM IR이 된다.

### LinearAnalysis

세 패스가 함께 쓰는 정수·index 값의 분석이다(`include/lcc-opt/Passes.h`, 정의는 `lib/ScfToAffine.cpp`). 값을
`c0 + Σ c_k·x_k`로, 넘침을 포함한 실제 값과 정확히 같게 나타낸다. `arith.addi`·`subi`, 상수와의 `muli`·`shli`,
`extsi`·`extui`·`trunci`, `index_cast`·`index_castui`를 따라가되, 피연산자의 범위로 결과가 넘치지 않거나 캐스트가 값을
바꾸지 않음을 보일 때만 따라가고 아니면 그 연산의 결과를 잎(x_k)으로 둔다. 잎의 범위는 그 형 전체지만 `scf.for`(와
상수 경계 `affine.for`)의 유도 변수는 본문이 도는 동안 [하한, 상한 - 1], `extui`·`andi`·`shrui`의 결과는 음이 아닌
범위다.

## 업스트림 affine 변환

`lcc-to-affine`의 결과에 업스트림 `affine-loop-tile`과 `affine-super-vectorize`를 쓸 수 있다(`convfix.c`에서 타일링은
바깥 두 루프를 8×8로 나누고, 벡터화는 곱-합 루프를 `vector<16xi8>` 읽기와 `vector.reduction <add>`로 바꾸며, x86_64와
aarch64에서 결과가 gcc -O2와 같다). 다만 MLIR 23의 `affine-super-vectorize`에는 다음 문제가 있어 시험 묶음에서 틀린
결과나 크래시가 난다. 모두 업스트림 `mlir-opt`만으로 재현된다.

- 보폭이 1이 아닌 접근(`m[k * 64]`, 복소수 배열의 `z[2 * i]`)을 연속 접근으로 벡터화한다.
- 횟수가 벡터 크기의 배수가 아닌 루프(예: 5회, 8폭)를 마스크 없이 `in_bounds = [true]`로 벡터화해 루프 범위 밖 원소에
  쓴다.
- `vectorize-reductions=true`에서 유도 변수 값을 쓰는 리덕션(`s += i`)을 만나면 죽는다.

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
