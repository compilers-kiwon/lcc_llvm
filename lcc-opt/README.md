# lcc-opt

lcc의 MLIR 백엔드(`-target=mlir/T`)가 낸 `.mlir`을 업스트림 MLIR의 최적화(affine, linalg 등)가 다룰 수 있는 모양으로 올리는
도구다. MLIR 23으로 만든 `mlir-opt`이며, `MlirOptMain` 위에 업스트림의 방언·확장·패스를 모두 등록하고 lcc-opt의 패스와 파이프라인을
더한다. 파이프라인은 다섯이다. `lcc-prepare`는 lcc가 지역 변수마다 만든 alloca를 SSA 값으로 바꾸고 호출을 인라인하고,
`lcc-to-scf`는 그 뒤 루프를 `scf.while`과 `scf.for`로 올린다. `lcc-to-affine`은 전역 배열을 `memref.global`로 바꾸고 그 배열만
다루는 루프 묶음을 `affine.for`로, `lcc-to-linalg`는 그 배열을 C 배열의 모양으로 보고 리덕션 둘레의 루프를 나눈 뒤 루프 묶음을
linalg 연산으로 올린다. `lcc-lower`는 이 결과를, 업스트림 변환과 transform 스크립트 뒤에도, `mlir-translate`가 받는 LLVM 방언으로
내린다([파이프라인](#파이프라인)).

lcc와 따로 빌드하는 프로그램이다. lcc의 코드는 들어 있지 않고 라이선스도 다르다([라이선스](#라이선스)). lcc 드라이버는 lcc-opt를
부르지 않으므로 명령을 차례로 쓴다([사용](#사용)).

## 필요한 것 (Ubuntu 24.04 기준)

| 패키지 | 쓰는 곳 |
| :- | :- |
| `libmlir-23-dev` | MLIR 헤더, CMake 설정, `libMLIR.so` |
| `llvm-23-dev` | LLVM 헤더, CMake 설정, `libLLVM.so`. 시험에 쓰는 `lit`·`FileCheck`(`llvm-23-tools`)도 함께 설치된다 |
| `mlir-23-tools` | MLIR의 CMake 설정이 가리키는 `mlir-tblgen` 등. 비교용 `mlir-opt`, `mlir-translate` |
| `cmake`, `ninja-build` | 빌드 |
| g++ 13 또는 clang 23 | C++17 컴파일러 |
| `clang-23`, `libclang-rt-23-dev` | ASan·UBSan으로 빌드할 때 |
| gcc, `gcc-aarch64-linux-gnu`, `libc6-dev-arm64-cross`, `qemu-user` | `kernels/check.sh`(aarch64는 교차 gcc와 qemu) |

LLVM 23 패키지는 [apt.llvm.org](https://apt.llvm.org)에 있다. 다른 곳에 설치한 MLIR을 쓰려면 `-DMLIR_DIR=…/lib/cmake/mlir`를
준다. MLIR 23이 아니거나 `libMLIR.so`·`libLLVM.so`가 없으면 CMake가 오류를 낸다.

## 빌드

저장소 최상위에서:

```bash
cmake -G Ninja -S lcc-opt -B build-lcc-opt
ninja -C build-lcc-opt
```

결과는 `build-lcc-opt/bin/lcc-opt`다. 패키지의 `mlir-opt`처럼 `libMLIR.so`와 `libLLVM.so`에 동적 링크한다. 정적 MLIR 라이브러리를
함께 링크하면 MLIR이 두 벌 들어가므로 쓰지 않는다. 우리 패스와 파이프라인은 `lib/`의 라이브러리 `LccOptPasses`로 따로 빌드해 도구에
링크한다. ASan·UBSan으로 빌드하는 명령은 [CI의 스크립트](#ci)에 있다.

## 사용

명령은 저장소 최상위에서 쓴다. mlir 백엔드로 빌드한 lcc(`etc/mlir-T.c`, 빌드 디렉터리 `$B`)로 `.mlir`을 만들고, lcc-opt의
파이프라인과 업스트림 변환을 거쳐 `lcc-lower`로 내린 것을 bytecode로 받아, LLVM 23의 `mlir-translate`와 clang으로 오브젝트를
만든다.

```bash
LCCDIR=$B $B/lcc -S foo.c                                     # foo.mlir
build-lcc-opt/bin/lcc-opt --lcc-to-scf --lcc-lower --emit-bytecode foo.mlir -o foo.mlirbc
/usr/lib/llvm-23/bin/mlir-translate --mlir-to-llvmir foo.mlirbc -o foo.ll
/usr/lib/llvm-23/bin/clang -O2 -c foo.ll                      # foo.o
```

`--lcc-to-scf` 자리에는 다른 파이프라인을 쓸 수 있고, 업스트림 패스는 `mlir-opt`과 같은 이름으로 `--lcc-lower` 앞에
둔다(`--lift-cf-to-scf`, `--affine-loop-tile` 등, 목록은 `lcc-opt --help`). `mlir-opt`과 달리 업스트림의 시험용 방언과
패스(`--test-*` 등)는 없다. lcc 드라이버가 부르는 `mlir-opt`는 arith·cf·func만 내리지만 lcc-opt의 출력에는 `scf`,
`lift-cf-to-scf`가 만든 `ub.poison`, affine·memref·linalg·vector가 남을 수 있으므로 `lcc-lower`로 내린다. 다른 도구에 넘길 때는
텍스트가 아니라 bytecode(`--emit-bytecode`)로 넘긴다. MLIR 23의 텍스트 형식은 다시 읽을 때 바꾸는 값이 있어, lcc-opt는 텍스트로 쓸
때 그런 값이 있으면 오류를 낸다([`lcc-check-text`](#lcc-check-text)).

`lcc-to-affine`은 고정 크기 파일 범위 배열을 상수 경계의 `for` 루프로 다루는 커널(저장소 README의
[포팅 규약](../README.md#포팅-규약))을 affine으로 올린다. 업스트림 affine 변환을 그 뒤에 둔다.

```bash
build-lcc-opt/bin/lcc-opt --lcc-to-affine foo.mlir           # affine.for, affine.load, affine.store
build-lcc-opt/bin/lcc-opt --lcc-to-affine --affine-loop-tile=tile-sizes=8,8 \
    '--affine-super-vectorize=virtual-vector-size=16 vectorize-reductions=true' \
    --lcc-lower --emit-bytecode foo.mlir -o foo.mlirbc
```

`--lcc-to-affine=remarks=true`를 주면 전역마다, 루프마다 바꾸었는지, 아니면 왜 남겼는지를 remark로
알린다(`--mlir-print-op-on-diagnostic=false`를 함께 주면 짧다). 업스트림 `affine-super-vectorize`에는 MLIR 23에서 틀린 코드를
내거나 죽는 경우가 있고, `affine-loop-fusion`처럼 루프 묶음을 다른 연산 너머로 옮기는 패스는 쓰면 안
된다([업스트림 affine 변환](#업스트림-affine-변환)).

`lcc-to-linalg`는 그 결과를 linalg로 올린다. 옵션 `remarks`는 `lcc-to-affine`의 것과 같고, `max-buffer-bytes`는
`lcc-distribute-reductions`의 버퍼 상한이다. linalg 연산의 타일링·벡터화는 transform 방언 스크립트로 하고, 스크립트는
`--transform-preload-library`로 준다. 스크립트를 페이로드 안에 두면 transform 모듈이 남아 `mlir-translate`가 읽지 못한다.

```bash
build-lcc-opt/bin/lcc-opt --lcc-to-linalg foo.mlir           # linalg.matmul, linalg.generic 등
build-lcc-opt/bin/lcc-opt --lcc-to-linalg \
    --transform-preload-library=transform-library-paths=lcc-opt/kernels/tile-vector.mlir \
    --transform-interpreter --lcc-lower --emit-bytecode foo.mlir -o foo.mlirbc
```

[kernels/](kernels)에 스크립트가 셋 있다. [tile.mlir](kernels/tile.mlir)은 `linalg.matmul`을 4x8x4, `linalg.conv_2d`를 출력
4x8과 창 2x2, 루프가 둘인 연산(`linalg.broadcast`, 원소별 `linalg.generic`, `linalg.fill`, `linalg.copy`)을 4x4로 타일링한다.
`convfix.c`의 matmul은 반복 차원이 `o`, `p`, `i` 순서이므로 4x8x4는 `o` 4개, `p` 8개, `i` 4개다.
[tile-vector.mlir](kernels/tile-vector.mlir)은 matmul과 루프가 둘인 연산을 같은 크기로 타일링해
`transform.structured.vectorize`로 벡터화하고, [mask-vector.mlir](kernels/mask-vector.mlir)은 그것들을 나누어떨어지지 않는
5x7x3과 5x7로 타일링해 같은 크기의 벡터로, 끝의 작은 타일은 마스크를 써서 벡터화한다. 두 스크립트 모두 `linalg.conv_2d`는 출력만
타일링한다([업스트림 linalg 변환과 transform](#업스트림-linalg-변환과-transform)).

## 파이프라인

| 이름 | 패스 | 하는 일 |
| :- | :- | :- |
| `lcc-prepare` | lcc-memcpy-intrinsic, mem2reg, sroa, mem2reg, canonicalize, lcc-byval-copy, inline, lcc-hoist-allocas, lcc-split-aggregates, mem2reg, sroa, mem2reg, canonicalize, cse | lcc가 지역 변수와 매개변수마다 만든 `llvm.alloca`를 SSA 값으로 바꾸고 호출을 인라인한다 |
| `lcc-to-scf` | lcc-prepare, lift-cf-to-scf, lcc-lift-llvm-func, canonicalize, cse, lcc-rotate-do-while, canonicalize, lcc-uplift-while, canonicalize, loop-invariant-code-motion, canonicalize, cse | 루프를 `scf.while`로, 셀 수 있는 루프는 `scf.for`로 올린다 |
| `lcc-to-affine` | lcc-to-scf, lcc-index-loops, lcc-globals-to-memref, canonicalize, cse, lcc-scf-to-affine, canonicalize, cse | 셀 수 있는 루프를 index `scf.for`로, 전역 배열을 1차원 `memref.global`로, 그 배열만 affine 첨자로 다루는 루프 묶음을 `affine.for`로 올린다. 옵션 `remarks` |
| `lcc-to-linalg` | lcc-to-affine, lcc-expand-shapes, lcc-distribute-reductions, canonicalize, cse, lcc-affine-to-linalg, linalg-specialize-generic-ops, canonicalize, cse | 함수마다 1차원 전역 memref를 C 배열의 모양으로 보고, 리덕션 둘레의 루프를 처음 값·리덕션·결과의 세 묶음으로 나눈 뒤, 묶음 자신의 루프 변수의 affine 첨자로만 읽고 쓰는 루프 묶음을 `linalg.generic`으로, 그중 이름 있는 연산(matmul, conv_2d, broadcast, fill, copy 등)은 그것으로 바꾼다. 옵션 `remarks`, `max-buffer-bytes` |
| `lcc-lower` | convert-linalg-to-loops, lower-vector-mask, lower-vector-multi-reduction, convert-vector-to-scf{full-unroll}, convert-vector-to-llvm, expand-strided-metadata, lower-affine, convert-scf-to-cf, convert-ub-to-llvm, finalize-memref-to-llvm, convert-arith-to-llvm, convert-cf-to-llvm, convert-func-to-llvm, reconcile-unrealized-casts, lcc-distinct-successors | `lcc-to-scf`·`lcc-to-affine`·`lcc-to-linalg`의 출력을, 업스트림 affine 변환과 linalg를 타일링·벡터화하는 transform 스크립트 뒤에도, `mlir-translate`가 받는 LLVM 방언으로 내린다 |

텍스트로 쓸 때는 파이프라인 끝에 [`lcc-check-text`](#lcc-check-text)가 더 돈다.

MLIR의 인라이너는 인자 처리를 callee의 방언에, 인라인한 블록의 처리를 호출 연산의 방언에 맡긴다. LLVM 방언은
`llvm.func`·`llvm.call`이면 `llvm.byval` 인자를 caller 쪽에 복사하고 callee의 상수 크기 `llvm.alloca`를 caller의 진입 블록으로
옮기지만, lcc가 대부분의 함수에 쓰는 `func.func`·`func.call`에는 func 방언이 둘 다 하지 않는다. 그대로 인라인하면 구조체를 값으로
받는 함수가 caller의 구조체를 고치고, 루프 안에서 인라인된 함수의 지역 배열은 반복마다 스택을 더 쓴다(256바이트 지역 배열을 쓰는
함수를 1000만 번 부르는 루프가 세그폴트를 냈다). 그래서 inline 앞뒤에 `lcc-byval-copy`와 `lcc-hoist-allocas`를
둔다([패스](#패스)).

lcc는 구조체 복사를 `llvm.call_intrinsic "llvm.memcpy…"`로, 구조체·배열 지역 변수를 `!llvm.array<N x i8>`로 낸다. sroa·mem2reg는
앞의 것을 주소가 빠져나가는 호출로 보고, 뒤의 것은 원소인 바이트를 가로지르는 load·store 때문에 나누지 못한다. 그래서
`lcc-memcpy-intrinsic`과 `lcc-split-aggregates`를 두고, sroa가 나눈 alloca를 올리도록 sroa 뒤마다 mem2reg를 한 번 더 둔다.
x86_64 시험 묶음(`src/run-llvm.sh`의 lcc, unit, c-testsuite, torture, llvm-bench)의 lcc 출력 830개 파일에서는 alloca 9031개
가운데 902개가 `lcc-prepare` 뒤에 남는다. 남는 것은 같은 바이트를 다른 형으로 읽고 쓰는 것(레지스터로 받은 구조체 인자를 `i64` 등으로
통째 저장하고 필드로 읽는 ABI 강제 변환, 공용체), 주소가 빠져나가는 것(호출 인자, 포인터 비교에 쓰는 `ptrtoint`, 저장, 블록 인자 등),
가변 첨자 배열, 구조체형 값을 읽고 쓰는 ABI 코드, `va_list`, volatile 등이다. 앞의 것은 정수 조각으로 나누면 올릴 수 있지만
lcc-opt는 나누지 않는다.

`lcc-to-scf`에서 `lift-cf-to-scf`는 루프 본문을 `before` 영역의 `scf.if` 안에 두는데, 뒤의 canonicalize(MLIR 23의
`WhileMoveIfDown`)가 그것을 `after` 영역으로 내린다. 업스트림 `lift-cf-to-scf`는 `func.func`만 올리므로, lcc가 가변 인자 함수 등에
쓰는 `llvm.func`는 `lcc-lift-llvm-func`가 올린다.

LLVM IR의 phi는 같은 선행 블록에서 오는 서로 다른 값을 구별하지 못하므로, 한 분기가 같은 블록에 블록 인자를 넘기며 여러 번
가면(switch의 여러 case가 같은 블록에 다른 값을 넘기는 등) `mlir-translate`가 LLVM IR로 내보내기 전에 두 번째부터 사이 블록을 넣어
나눈다. MLIR 23의 이 코드(`LegalizeForExport.cpp`의 `ensureDistinctSuccessors`)는 그런 목적지를 블록 포인터를 키로 한 해시
맵(`SmallDenseMap`)에 모아 그 순서대로 사이 블록을 만들므로, 한 분기에 그런 목적지가 둘 이상이면 LLVM IR의 블록 순서와 phi 피연산자
순서가 실행마다 달라진다(같은 입력 파일로도 재현되고 뜻은 같다). `lcc-lower`는 끝에 `lcc-distinct-successors`를 두어 같은 나누기를
후속 블록 순서대로 먼저 하므로 `mlir-translate`가 나눌 것이 남지 않는다. `lcc-prepare`만 돌리고 내리면 mem2reg가 만든 블록 인자
때문에 이런 switch가 생긴다.

`lcc-lower`의 앞쪽 패스들은 linalg와 vector를 내린다. linalg를 루프로 내리는 `convert-linalg-to-loops`와 벡터화한 linalg가 남기는
`vector.mask`·`vector.multi_reduction`을 내리는 패스가 맨 앞이다. `convert-vector-to-scf`는 `full-unroll`로 돌린다. 그렇지
않으면 2차원 이상의 transfer에 임시 버퍼를 가장 가까운 `AutomaticAllocationScope`의 처음에 `memref.alloca`로 두는데, 타일링한
코드에서 그것은 `scf.for`의 본문이고, `convert-scf-to-cf`는 그것을 stacksave/stackrestore 없이 루프 안의 `alloca`로 만들어
반복마다 스택이 자란다(시험 묶음의 FloatMM이 일반 transform 스크립트로 스택을 넘쳤다. 업스트림 `mlir-opt`만으로 재현되고, 업스트림
이슈는 [llvm/llvm-project#223187](https://github.com/llvm/llvm-project/issues/223187)). `convert-vector-to-llvm`은
`convert-scf-to-cf`보다 앞에 둔다. 이 패스는 변환 전에 모듈 전체를 탐욕적으로 다시 쓰는데, `convert-scf-to-cf` 뒤에 두면 블록을
합쳐 case들이 같은 블록에 블록 인자를 넘기는 switch를 만든다. 앞에 두면 벡터가 없는 코드에서 이 앞쪽 패스들은 LLVM IR을 바꾸지
않는다(x86_64 시험 묶음의 lcc 출력 830개 파일, `lcc-to-scf`·`lcc-to-affine` 뒤에서 확인).

`lcc-to-affine`의 세 패스는 포팅 규약을 어긴 코드를 틀리게 컴파일하지 않도록, 바꾼 뒤에도 같은 값이 됨을 상수와 값의 범위로 보일 수
있을 때만 바꾸고 C의 부호 있는 넘침이 정의되지 않는다는 것에는 기대지 않는다. affine 분석은 다른 memref끼리 겹치지 않는다고 보고 다른
메모리 연산은 보지 않으므로, 루프 묶음 안에 호출이나 포인터를 거친 접근이 하나라도 있으면 올리지 않는다. x86_64 시험 묶음의 lcc 출력
830개 파일에서는 전역 157개(실수 표 포함)가 `memref.global`이 되고 루프 묶음 49개(루프 79개)와 다른 루프 안의 묶음 7개가 올라간다.
시험 묶음은 규약대로 쓴 코드가 아니어서, 남는 루프는 대부분 포인터 매개변수를 거친 접근(`llvm.getelementptr`), `llvm.func`(가변 인자
함수 등), 호출 때문이다.

`lcc-to-linalg`의 세 패스도 같은 원칙을 따른다. 바꾼 뒤에도 같은 원소, 같은 순서임을 루프 경계와 affine 의존성 분석으로 보일 때만
바꾸고, 보이지 못하면 affine으로 둔다. 같은 830개 파일에서는 함수마다 전역 20개가 보기로 펼쳐지고(4개는 묶음마다 모양이 다르거나
계수가 크기를 나누지 않아 1차원으로 남는다), 리덕션 둘레의 루프 6곳(iter_args 리덕션 2, 메모리에 누적하는 행렬 곱 4)이 나뉘며, 루프
묶음 25개가 linalg(`linalg.fill` 14, `linalg.generic` 7, `linalg.matmul` 4)가 된다. iter_args 리덕션은 대부분 둘레 루프 없이
함수 본문에 있어(18) 나누지 않는다. 남는 묶음은 주로 그런 리덕션(18), 바깥 루프의 변수를 첨자에 쓰는 안쪽 묶음(14), 첨자가 루프 변수의
합인 출력(12) 때문이다.

## 패스

### `lcc-memcpy-intrinsic`

lcc의 `llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"`(32비트 타깃은 `.i32`)를, volatile 인자가 상수이면 같은
피연산자·volatile·정렬(`arg_attrs`)의 `llvm.intr.memcpy`로 바꾼다. lcc는 정렬을 주려고 `call_intrinsic`을 쓴다(lcc의 mlir
백엔드를 만든 MLIR 21의 `llvm.intr.memcpy`에는 정렬을 줄 수 없었다).

### `lcc-split-aggregates`

`llvm.alloca 1 x !llvm.array<N x i8>`의 쓰임이 모두 상수 첨자 `getelementptr … i8`을 거친 정수·실수·포인터의 non-volatile
load·store이거나, 다른 포인터와의 상수 길이 non-volatile `llvm.intr.memcpy`이면, 멤버(오프셋과 형)마다 스칼라 alloca로 나눈다.
load·store끼리는 같은 오프셋·같은 형이 아니면 겹치지 않아야 하고, memcpy가 멤버를 일부만 덮으면 안 되며, load·store가 하나는 있어야
한다. memcpy는 범위 안의 자리마다 load와 store로 바뀌고, 멤버가 덮지 않는 바이트(패딩, 쓰지 않는 멤버)는 8바이트 이하 정수 채움
자리가 되어 모든 바이트가 그대로 복사된다. 자리는 64개까지. 나눈 alloca는 mem2reg가 올린다.

### `lcc-byval-copy`

인라이너 앞에서, 본문이 있는 `func.func`의 `llvm.byval` 인자마다 진입 블록에 그 타입의 `llvm.alloca`(정렬은 타입의 ABI 정렬과
인자의 `llvm.align` 중 큰 것)와 인자에서의 `llvm.intr.memcpy`를 두고 본문이 복사본을 쓰게 한다. byval 인자는 이미 callee의
복사본이므로 뜻은 같고, 인라인되면 이 복사가 호출마다 caller 쪽에서 일어난다. 인라인되지 않은 함수에는 복사가 하나 더 남는다. 인자를
쓰는 곳이 그런 memcpy 하나뿐이면 다시 복사하지 않는다.

### `lcc-hoist-allocas`

인라이너 뒤에서, `func.func`·`llvm.func` 본문의 진입 블록이 아닌 블록에 있는 상수 개수 `llvm.alloca`를 진입 블록 처음으로 옮긴다.
lcc는 alloca를 모두 진입 블록에 두므로(x86_64 시험 묶음의 lcc 출력 830개 파일, alloca 9031개 모두), 인라이너 뒤 진입 블록 밖의
alloca는 인라인된 함수의 것이다. 옮긴 alloca는 뒤의 mem2reg·sroa가 올릴 수 있다. 안쪽 영역의 alloca는 다른 할당 범위일 수 있어 두고,
LLVM 방언과 달리 lifetime 표시를 넣지 않으므로 옮긴 alloca끼리 스택 자리를 나눠 쓰지 않는다.

### `lcc-lift-llvm-func`

업스트림 `lift-cf-to-scf`는 `func.func`만 보고, 출구 없는 무한 루프 뒤에 둘 종결로 `func.return`을 만든다. lcc는 가변 인자 함수,
원형 없이 먼저 불린 함수, `-Wf-llvmfunc`이면 모든 함수를 `llvm.func`로 낸다. 이 패스는 같은
변환(`ControlFlowToSCFTransformation`)을 분기가 모두 `cf.br`·`cf.cond_br`·`cf.switch`(lcc가 쓰는 것)인 `llvm.func` 본문에
적용하고, 무한 루프 뒤에는 `llvm.return`(값을 돌려주는 함수면 poison 값)을 둔다. 출구 종류가 하나로 남아야 `cf`가 남지 않는다. 다른
분기(`llvm.br` 등)가 있는 함수는 그대로 둔다.

### `lcc-rotate-do-while`

lcc는 `for (e1; e2; e3) S`를 `e1; goto 시험; L: S; e3; 시험: if (e2) goto L`로 내리고, 첫 시험이 참임을 증명하면
(`for (i = 0; i < 16; i++)`처럼 상수끼리) 첫 점프를 뺀다. 그러면 do-while 모양이 되어 `lift-cf-to-scf`가 `before` 영역에 본문과
시험을 모두 두고 `after` 영역은 값을 넘기기만 하는 `scf.while`을 만든다. 이 패스는 본문을 `after` 영역으로 옮기고 `before`
영역에서는 유도 변수 자체를 시험한다. 다음을 모두 만족할 때만 바꾼다.

- `after` 영역은 블록 인자를 `scf.yield`로 넘기기만 한다(순서는 바뀌어도 된다).
- 조건이 `before` 블록 인자 `iv` 자리로 넘어가는 값과 루프 밖 값 `ub` 사이의 `arith.cmpi`다. 술어와 `iv`의 갱신식은 가리지 않고,
  `ub`가 왼쪽이어도 된다.
- 첫 시험(`iv`의 초기값과 `ub`의 비교)이 상수로 참이다. 그래야 do-while 루프에서 늘 한 번은 도는 본문이 바꾼 뒤에도 한 번은 돈다.

두 번째부터의 시험은 원래 루프가 비교하던 바로 그 값끼리 비교하므로, 술어와 갱신식이 무엇이든 본문이 도는 횟수가 같다. `after` 영역이
넘기지 않는 조건 인자(마지막 반복의 값을 루프 뒤에서 쓰는 것)는, 루프 밖에서 정의됐거나 루프 밖 값만 쓰는 순수 연산으로 계산되면 그
연산을 루프 앞으로 옮겨 그 값으로 바꾸고, 그렇지 않으면 초기값이 0(`arith.constant`, 포인터 등은 `llvm.mlir.zero`)인 루프 운반 값을
하나 더한다. 본문이 적어도 한 번 돌므로 그 초기값은 쓰이지 않는다. 0을 만들 수 없는 형(텐서 등)이면 바꾸지 않는다.

### `lcc-uplift-while`

`before` 영역이 `before` 블록 인자 `iv`와 루프 밖 값의 `arith.cmpi` 하나와 모든 인자를 넘기는 `scf.condition`이고, `after` 영역이
`iv`에 `iv + s`나 `iv - s`(`s`는 상수)를 넘기는 `scf.while`을 `scf.for`로 바꾼다. `scf.for`는 마지막 유도 변수가 형에 들어가지
않는 루프를 정의하지 않지만 `scf.while`은 그냥 감싸므로, `iv`가 감싸지 않는다는 것을 상수로 보일 수 있을 때만 바꾼다. C의 부호 있는
넘침이 정의되지 않는다는 것에는 기대지 않는다.

| 꼴 | 조건 | 결과 |
| :- | :- | :- |
| `iv < ub` (slt, ult), 증가 | `s = 1`, 또는 `ub`가 상수이고 `ub + s - 1`이 형에 들어감 | `scf.for [unsigned] iv = init to ub step s` |
| `iv <= ub` (sle, ule), 증가 | `ub`가 상수이고 `ub + s`가 형에 들어감 | `iv < ub + 1`로 위와 같이 |
| `iv != ub` (ne), 증가 | `s = 1`이고 `init <= ub`(상수로, 부호 있게나 없게) 또는 `ub`가 형의 최댓값 | `iv < ub`로 위와 같이 |
| `iv > lb` (sgt, ugt), 감소 | `s = 1`, 또는 `lb`가 상수이고 `lb - s + 1`이 감싸지 않음 | `scf.for unsigned k = 0 to n step 1`, `iv = init - k * s` |
| `iv >= lb` (sge, uge), 감소 | `lb`가 상수이고 `lb - s`가 감싸지 않음 | `iv > lb - 1`로 위와 같이 |
| `iv != lb` (ne), 감소 | `s = 1`이고 `init >= lb`(상수로) 또는 `lb`가 최솟값(`u != 0` 등) | `iv > lb`로 위와 같이 |

감소하는 루프의 횟수 `n`은 `init > lb`면 `(init - lb - 1) / s + 1`, 아니면 0이다. 루프 뒤의 유도 변수(`scf.while`의 결과)는
증가하면 `init + n * s`(`n`은 `ub > init`면 `(ub - init - 1) / s + 1`, 아니면 0), 감소하면 `init - n * s`이고, `iv`와 같은 폭의
부호 없는 산술로 계산해도 결과가 형에 들어가므로 정확하다. `for (i = 0; i < n; i += 4)`처럼 step이 1보다 크고 경계가 변수인 증가
루프는 `i`가 `INT_MAX`를 넘어 감쌀 수 있으므로 바꾸지 않는다.

업스트림 `populateUpliftWhileToForPatterns`는 `slt`·`sgt` 증가만 다루고, 실행 시간에 0이거나 음수일 수 있는 step도
받으며([llvm/llvm-project#225256](https://github.com/llvm/llvm-project/issues/225256)), 루프 뒤의 유도 변수를 한 step
모자라게(한 번도 돌지 않으면 `lb`가 아니게)
준다([llvm/llvm-project#219616](https://github.com/llvm/llvm-project/issues/219616), main에서 고쳤지만 MLIR 23.1.6에는 없다).
예를 들어 `for (i = 0; i < 10; i += 3)` 뒤의 `i`는 12인데 업스트림 패턴은 9를 준다. 그래서 쓰지 않는다.

### `lcc-index-loops`

정수형 `scf.for`(lcc의 C 루프 변수 형, 대개 `i32`)를 `index` 루프로 바꾼다. `scf.for`에서 유도 변수는 `lb + j * s`를 정확히 계산한
값이고 `lb + n * s`(n은 횟수)가 형에 들어가야 하므로, 경계와 step을 부호 확장해도(unsigned 루프는 0 확장) 같은 값으로 같은 횟수를
돈다. 그래서 index가 iN의 모든 값을 담으면 바꾸고, `scf.for unsigned`는 부호 있는 index 루프가 되므로 경계가 부호 있는 수로도 음이
아니어야 한다: N이 index 폭보다 작으면 늘 그렇고, 같으면(`i64`) 경계의 범위가 음이 아니고 `ub + step - 1`이 넘치지 않음을 보일 때만
바꾼다. 본문은 index 유도 변수를 iN으로 되돌린 값을 쓰고, 유도 변수의 `extsi`(unsigned 루프는 `extui`)와 index 캐스트는 index 유도
변수의 캐스트로 바꾼다. 유도 변수의 값이 iN에 들어가므로 같은 값이다. MLIR의 index 폭은 데이터 배치 명세(DLTI)가 없으면 64비트라서,
32비트 타깃에서도 유도 변수는 64비트 산술이 된다.

### `lcc-globals-to-memref`

lcc는 전역 배열을 `!llvm.array<N x i8>`(0이 이어지는 초기값은 바이트 배열과 0의 packed 구조체, 긴 것은 256개씩 중첩)로 내고, 원소는
`llvm.mlir.addressof`와 바이트 오프셋의 `llvm.getelementptr`, `llvm.load`·`llvm.store`로 읽고 쓴다. 이 패스는 그런 전역을
`memref<(N / sizeof(T)) x T>`의 1차원 `memref.global`로, 그 load·store를 원소 첨자의 `memref.load`·`memref.store`로 바꾼다. T는
그 전역의 모든 load·store의 형이다. 다음을 모두 만족하는 전역만 바꾼다.

- 위의 바이트 배열이다. 스칼라 변수(lcc는 제 형으로 낸다), 포인터 배열, 크기가 0인 불완전 배열 선언이 아니다.
- 링크가 external이나 internal이다(lcc의 문자열 상수와 지역 배열 초기값은 private이라 바꾸지 않는다). 주소 공간 0,
  section·comdat·thread_local·다른 가시성이 없다.
- 이름은 함수 본문의 `llvm.mlir.addressof`만 쓴다. 다른 전역의 초기값(과 제 초기값)이 주소를 쓰지 않는다. 함수 속성 안에서
  심볼로 가리키는 전역(lcc `-Wf-pragma=NS`의 `NS.args`)도 바꾸지 않는다(remark `name used by func.func`).
- load·store가 있고 모두 같은 형 T이며(타입 퍼닝이 아님), T는 8·16·32·64비트 정수나 `f32`·`f64`이고, N이 sizeof(T)의 배수다.
- 초기값이 `llvm.mlir.zero`(→ `dense<0>`), 주소 조각이 없는 바이트 덩어리(→ 리틀 엔디언으로 푼 T의 `dense`, 음수 포함), 또는
  없다(선언 → 초기값 없는 `memref.global`). `f32`·`f64` 표는 바이트를 비트 그대로 풀어 −0.0, NaN의 페이로드와 부호, 무한대, 비정규
  수가 남는다. NaN·무한대는 텍스트에서 16진으로, 다른 값은 다시 읽어도 같은 십진수로 찍혀 `lcc-check-text`도 통과한다. 빅 엔디언의
  바이트 초기값은 바꾸지 않는다.

external은 public, internal은 private가 되고(memref를 내리면 LLVM private 링크), constant와 정렬은 그대로다. load·store는
volatile·원자 연산이 아니고 sizeof(T)보다 작은 정렬 같은 다른 속성이 없으며, 주소가 전역에 바이트 오프셋을 더한 것이고 그 합이
LinearAnalysis로 `c0 + Σ c_k·x_k`(c0과 모든 c_k가 sizeof(T)의 배수)임을 포인터의 인덱스 폭에서 정확히 보일 때 memref 접근이 된다.
첨자는 index 산술의 `c0 / sizeof(T) + Σ (c_k / sizeof(T))·x_k`다. 32비트 타깃에서 lcc는 첨자에 원소 크기를 `i32`로 곱한 뒤
확장하므로(넘칠 수 있음) 루프 밖 `a[k]`처럼 범위를 모르는 접근은 바꾸지 않는다. 그 밖의 쓰임(volatile 접근, 호출 인자, 저장된 주소,
넘칠 수 있는 오프셋 등)은 memref에서 만든 포인터(`memref.extract_aligned_pointer_as_index`, `arith.index_cast`,
`llvm.inttoptr`)를 쓴다. memref 접근을 내리면 `getelementptr inbounds|nuw`가 되므로 배열 밖 접근은 LLVM에서도 정의되지
않는다(C에서도 정의되지 않는다).

### `lcc-scf-to-affine`

index `scf.for` 루프 묶음을 `affine.for`로, 그 안의 memref 접근을 `affine.load`·`affine.store`로 올린다. 바깥 루프부터 보며, 묶음
전체가 다음을 만족하는 첫 루프를 안쪽 루프와 함께 올린다. 아니면 그 안의 루프를 따로 본다.

- 모든 루프가 부호 있는 index `scf.for`이고 step은 양의 상수, 경계는 묶음의 바깥 루프 유도 변수(차원)와 심볼의 affine 식이다. 심볼은
  상수, 묶음 밖에서 정의된 유효한 affine 심볼, 또는 함수 최상위에서 정의된 정수(정의 바로 뒤에 `arith.index_cast`를 두어 쓴다)다.
  다른 루프 안에서 정의된 값은 MLIR에서 심볼이 될 수 없다.
- 묶음에는 부작용 없는 arith 연산, 상수, `ub.poison`, `scf.if`, 묶음의 안쪽 루프, 그리고 `memref.get_global`에서 온 1차원
  memref의 `memref.load`·`memref.store`만 있다. 첨자는 경계처럼 affine이고, 루프 경계로 계산한 첨자의 범위가 memref 안이어야 한다.
- affine 영역인 `func.func` 안이다(`llvm.func`는 아님).

호출, `llvm.load`·`llvm.store`, `llvm.alloca`, 포인터 연산 등이 하나라도 있으면 그 묶음은 올리지 않는다. 첨자 식은 직접 affine
맵으로 만든다. 업스트림 `affine-raise-from-memref`는 index의 `arith.addi`·`arith.muli`만 affine으로 알아보고 lcc 주소 계산의
`arith.shli`, 확장, index 캐스트는 알아보지 못하며, 올리지 못한 접근은 affine 루프 안에 `memref.load`·`memref.store`로 남기는데
affine 분석은 그것을 보지 않는다.

이 패스는 루프 묶음 안만 보므로, 같은 함수의 묶음 밖에 호출이나 포인터를 거친 접근이 있어도 묶음을 올린다. 그래서 루프 묶음을 다른 연산
너머로 옮기는 업스트림 패스(`affine-loop-fusion` 등)는 쓰면 안 된다([업스트림 affine 변환](#업스트림-affine-변환)).

### `lcc-expand-shapes`

lcc-globals-to-memref는 전역 배열을 1차원 `memref.global`로 만들고, lcc-scf-to-affine은 그것을 C 배열의 행 우선
첨자(`p * 16 + i`)로 읽고 쓴다. 이 패스는 `func.func`마다, 그 함수가 `memref.get_global`로 다루는 1차원 전역마다 첨자가 보이는
모양을 찾아 함수 처음에 `memref.expand_shape` 보기(view) 하나를 두고, 그 함수에서 그 전역을 읽고 쓰는
`affine.load`·`affine.store`·`memref.load`·`memref.store`를 모두 그 보기의 접근으로 바꾼다. affine 분석과 linalg는 서로 다른
memref 값을 겹치지 않는 메모리로 보므로, 한 함수에서 한 전역은 보기 하나로만 다루거나 펼치지 않는다.

모양은 affine 첨자의 계수로 정한다. 계수들과 1을 큰 것부터 s_1 > s_2 > … > 1로 놓아 각각이 바로 앞의 것을 나누고 s_1이 원소 수 N을
나누면 모양은 [N / s_1, s_1 / s_2, …, s_(k-1)]이다(`memref<1024xi8>`을 `p * 16 + i`로 읽으면 64x16). 첨자의 항은 계수를 나누는
가장 바깥 차원으로 가고, 상수는 안쪽 차원부터 그 차원 안에 남도록 나뉜다(`i`가 [1, 17)이면 `p * 16 + i - 1`은 [p, i - 1]). 첫
차원이 아닌 차원의 첨자가 루프 경계로 계산한 범위에서 그 차원 안에 있음을 보일 수 있어야 하고 첫 차원도 범위 안이어야 한다. 그렇지 않은
첨자(`i`가 [0, 16)인 `p * 16 + i + 1`처럼 행을 넘을 수 있는 것)가 하나라도 있거나, 계수가 포개지지 않거나(6과 4), 루프 묶음마다
필요한 모양이 다르면(한 묶음은 4x16, 다른 묶음은 8x8) 그 함수에서 그 전역은 1차원으로 둔다. affine 루프 밖의
`memref.load`·`memref.store`는 첨자 x를 [x floordiv s_1, (x mod s_1) floordiv s_2, …, x mod s_(k-1)]로 바꾼다. 어떤 x에서나
같은 원소이므로 x의 범위를 몰라도 되고 배열 밖 접근이 정의되지 않는다는 것에 기대지 않는다. 포인터를 만드는
`memref.extract_aligned_pointer_as_index`는 1차원 memref에 그대로 두고, 그 밖의 쓰임(`memref.cast` 등)이 있으면 그 함수에서 그
전역은 1차원으로 둔다.

### `lcc-distribute-reductions`

C의 리덕션

```c
for (p = 0; p < 64; p++)
	for (o = 0; o < 32; o++) {
		int acc = bias[o];
		for (i = 0; i < 16; i++)
			acc += w[o][i] * in[p][i];
		out[p][o] = clamp(acc);
	}
```

은 lcc-to-affine 뒤 `p`·`o` 루프 안의 iter_args `affine.for`가 된다. 이 패스는 리덕션 변수를 둘레 루프의 반복마다 칸이 하나씩 있는
버퍼(함수 처음의 `memref.alloca`, 여기서는 `memref<64x32xi32>`)로 펴고, 둘레 루프를 리덕션 루프 앞의 문장, 리덕션 루프, 뒤의 문장의
세 묶음으로 나눈다. 첫 묶음은 처음 값을 버퍼에 쓰고(`buf[p][o] = bias[o]`), 둘째 묶음은 리덕션 루프의 반복마다 버퍼를 읽어 계산하고
다시 쓰며(`buf[p][o] += w[o][i] * in[p][i]`, `i` 순서대로), 셋째 묶음은 결과를 읽어 쓴다(`out[p][o] = clamp(buf[p][o])`).
문장마다 제 묶음 안의 순서가 그대로이므로 float 리덕션도 같은 값을 같은 순서로 더한다. 리덕션 루프 안에 완전히 중첩되어 iter_args를
그대로 이어받는 리덕션 루프(`for (r…) for (s…) t += …`)는 같은 버퍼를 쓴다. 뒤 묶음이 앞 문장의 값을 쓰면, 루프 변수와 바깥 값의
부작용 없는 계산은 쓰는 묶음에서 다시 계산하고 그 밖의 것(load 등)은 버퍼로 편다. iter_args 없이 메모리에 누적하는 루프, 곧 자기 루프
변수에 기대지 않는 첨자에 저장하는 루프(`c[i][j] = 0; for (k…) c[i][j] += a[i][k] * b[k][j]`의 `k` 루프)의 둘레 루프도 버퍼 없이
같이 나눈다.

나누는 둘레 루프는 리덕션 루프를 둘러싼 루프와, 그 바깥으로 본문이 부작용 없는 연산과 다음 루프뿐인 루프들이다. 상수 경계에 반복이 있고
iter_args가 없어야 하며, 리덕션 루프 앞뒤의 문장은 `affine.load`, `affine.store`, 부작용 없는 연산이어야 한다. 그리고 affine 의존성
분석(`checkMemrefAccessDependence`)으로, 둘레 루프 안의 두 접근(하나는 store)이 둘레 루프의 서로 다른 반복 사이에서 어느 방향으로도
의존하지 않음을 보일 때만 나눈다. 그러면 어느 문장도 뒤 문장에 의존하지 않으므로 앞 문장 전부, 리덕션 전부, 뒤 문장 전부의 순서로
실행해도 모든 의존이 지켜진다. 같은 전역을 리덕션 앞에서 읽고 뒤에서 같은 원소에 쓰는 것은 한 반복 안의 의존이라 나누고, 다음 원소에
쓰는 것(다음 반복이 그것을 읽음)은 남긴다. 다음 원소를 읽고 이 원소에 쓰는 반대 방향은 나누어도 되지만, 둘레 루프가 옮기는 의존이
하나라도 있으면 남기므로 이것도 남긴다. 한 메모리를 서로 다른 memref 값으로 다루거나, 접근하는 memref가 `memref.get_global`이나
`memref.alloca`에서 나오지 않거나, 분석이 실패하면 남긴다. 버퍼는 스택에 있으므로 함수마다 모두 `max-buffer-bytes`(기본 65536)
바이트까지이고, 넘으면 그 리덕션은 affine으로 남는다. 안쪽 루프부터 라운드마다 나누므로, 한 라운드에서 생긴 묶음을 둘러싼 리덕션은 다음
라운드에서 나눈다.

### `lcc-affine-to-linalg`

완전히 중첩된 `affine.for` 묶음 가운데 가장 안쪽 본문이 묶음의 루프 변수의 affine 첨자로 memref를 읽고 쓰고 부작용 없는 연산으로
계산하기만 하는 것을, 루프마다 반복 차원 하나를 루프 순서대로 가진 `linalg.generic`으로 바꾼다. linalg는 이것을 같은 루프로 내린다.
다음을 모두 만족해야 바꾸고, 아니면 affine으로 둔다.

- 모든 루프가 상수 경계에 반복이 있고 iter_args가 없으며, 바깥 루프의 본문은 부작용 없는 연산과 다음 루프뿐이다.
- 모든 첨자가 묶음의 루프 변수 `i_k`의 `c + Σ a_k·i_k`(상수 `a_k ≥ 0`과 `c`)이고 memref(정적 모양, 항등 배치) 안에 있다.
- 쓰는 memref마다 `affine.store`가 하나이고 그 첨자는 서로 다른 루프 변수에 상수를 더한 것이며, 그 memref는 같은 첨자로만
  읽는다(누적하는 출력). store 뒤의 load는 저장한 값을 읽는다. 읽기만 하는 memref는 입력이고, 첨자가 다르면 다른 입력이다. 한
  메모리(같은 전역의 `memref.get_global`, 같은 `memref.alloca`)를 서로 다른 memref 값으로 다루거나,
  `memref.get_global`·`memref.alloca`에서 나오지 않은 memref가 있으면 남긴다.

반복 차원 `j_k`는 루프의 반복 번호(0부터)이고 루프 변수는 `lb + step·j_k`다. 첨자 `c + a·j_k`는 오프셋 `c`, 크기 반복 수, 보폭
`a`인 `memref.subview`의 첨자 `j_k`가 되고, 상수 첨자는 크기 1인 subview, 루프 변수의 합은 그 범위만 한 subview가 된다. 그래서
첨자가 `j_k` 하나뿐인 차원은 그 루프의 반복 수만큼 크고, linalg는 그런 차원에서 반복 수를 알아낸다. 그런 첨자가 없는 루프가 있으면
남긴다. 본문이 쓰는 루프 변수는 `lb + step·linalg.index`가 된다. 모든 출력의 첨자에 있는 차원은 parallel, 아니면 reduction이다.
출력의 첨자가 서로 다른 루프 변수이므로 parallel 차원의 서로 다른 반복은 서로 다른 출력 원소를 쓰고 입력은 쓰지 않아 서로 의존하지
않는다. 뒤의 `linalg-specialize-generic-ops`는 이름 있는 연산으로 바꿀 수 있는 것을 바꾼다. `convfix.c`의 곱-합은 입력을 C의 곱
순서(`w`, `in`)로 두므로, 반복 차원이 `o`, `p`, `i` 순서이고 출력 맵이 `(d1, d0)`인 `linalg.matmul`이 된다. 각 출력 원소의 덧셈은
여전히 `i` 순서다.

### `lcc-distinct-successors`

`mlir-translate`가 내보내기 전에 하는 나누기를 결정적인 순서로 먼저 한다. 종결 연산마다 후속 블록을 차례로 보며, 블록 인자가 있는
같은 블록이 두 번째부터 나오는 자리를 새 블록으로 돌린다. 새 블록은 영역 끝에 붙고, 같은 인자를 받아 원래 블록으로 `llvm.br`한다.
인자가 없는 블록은 phi가 필요 없어 반복되어도 둔다. 만드는 블록은 MLIR의 `ensureDistinctSuccessors`와 같고 순서만 정해져 있으므로,
그런 목적지가 하나뿐인 분기에서는 `mlir-translate`가 나누는 것과 같은 LLVM IR이 된다.

### `lcc-check-text`

MLIR 23의 텍스트 형식은 두 가지 값을 다시 읽을 때 바꾼다. parser가 십진 실수를 double로 읽으므로 double이 아닌 `f80`·`f128`
상수(예: `0.1L`)가 double 값이 되고, `cf.switch`의 printer가 case를 부호 없이 쓰므로 `i64`의 음수 case(예: `INT64_MIN`)는 다시
읽지 못한다(generic 형식은 부호대로 쓴다). 패스 없이 읽고 쓰기만 해도 그렇다. 그래서 lcc-opt는 텍스트로 쓸 때(`--emit-bytecode`가
없을 때) 파이프라인 끝에 이 패스를 돌려, 그런 값이 있으면 연산마다 오류를 내고 아무것도 쓰지 않는다. `--lcc-allow-lossy-text`를 주면
경고만 내고 쓴다. x86_64 시험 묶음의 lcc 출력 830개 파일에서 이 검사가 잡는 파일은 텍스트로 쓰고 다시 읽으면 LLVM IR이 달라지는
파일과 같다(lcc 출력 그대로 6개, `lcc-to-scf` 뒤 4개). 업스트림 main은 둘 다
고쳤지만([llvm/llvm-project#220267](https://github.com/llvm/llvm-project/pull/220267),
[llvm/llvm-project#221490](https://github.com/llvm/llvm-project/pull/221490)) MLIR 23.1.6에는 들어 있지 않다.

### LinearAnalysis

세 패스가 함께 쓰는 정수·index 값의 분석이다(`include/lcc-opt/Passes.h`, 정의는 `lib/ScfToAffine.cpp`). 값을 `c0 + Σ c_k·x_k`로,
넘침을 포함한 실제 값과 정확히 같게 나타낸다. `arith.addi`·`subi`, 상수와의 `muli`·`shli`, `extsi`·`extui`·`trunci`,
`index_cast`·`index_castui`를 따라가되, 피연산자의 범위로 결과가 넘치지 않거나 캐스트가 값을 바꾸지 않음을 보일 때만 따라가고 아니면
그 연산의 결과를 잎(x_k)으로 둔다. 잎의 범위는 그 형 전체지만 `scf.for`(와 상수 경계 `affine.for`)의 유도 변수는 본문이 도는 동안
[하한, 상한 - 1], `extui`·`andi`·`shrui`의 결과는 음이 아닌 범위다.

## 업스트림 affine 변환

`lcc-to-affine`의 결과에 업스트림 `affine-loop-tile`과 `affine-super-vectorize`를 쓸 수 있다. `convfix.c`에서
타일링(`tile-sizes=8,8`)은 바깥 두 루프를 8×8로 나누고, 벡터화(`vectorize-reductions=true`)는 곱-합 루프를 벡터 읽기와
`vector.reduction <add>`로 바꾼다. [kernels/check.sh](#kernels)가 다섯 커널에서 그 결과를 gcc와 비교한다. 다만 MLIR 23의
`affine-super-vectorize`에는 다음 문제가 있어 틀린 결과나 크래시가 난다. 모두 업스트림 `mlir-opt`만으로 재현된다.

- 보폭이 1이 아닌 접근(`m[k * 64]`, 복소수 배열의 `z[2 * i]`)을 연속 접근으로 벡터화한다. `vectorize-reductions=true`가 없으면
  `convfix.c`에서도 `o` 루프를 벡터화하며 16바이트씩 떨어진 `w[o][i]`를 이어진 원소처럼 읽고, 있어도 `kernels/mm.c`의 `mm2`는 `k`
  루프를 벡터화하며 8개씩 떨어진 `b[k][j]`를 그렇게 읽는다. 그래서 `vectorize-reductions=true`로 쓰고, 그래도 벡터화한 루프의
  접근은 따로 확인해야 한다.
- 횟수가 벡터 크기의 배수가 아닌 루프(예: 5회, 8폭)를 마스크 없이 `in_bounds = [true]`로 벡터화해 루프 범위 밖 원소에
  쓴다([llvm/llvm-project#218120](https://github.com/llvm/llvm-project/issues/218120)).
- `vectorize-reductions=true`에서 유도 변수 값을 쓰는 리덕션(`s += i`)을 만나면 죽는다.
- 리덕션을 벡터의 칸마다 따로 더하므로 float 합의 순서가 C와 달라 마지막 자리가 달라질 수 있다(`kernels/convff.c`에서 128값 중 24개,
  상대 차이 3.1e-7 이하).

루프 묶음을 다른 연산 너머로 옮기는 패스(`affine-loop-fusion` 등)는 쓰면 안 된다. lcc-opt는 루프 묶음 안만 보므로, 같은 함수의 묶음
밖에 호출이나 포인터를 거친 접근이 있어도 묶음을 올린다. x86_64 시험 묶음의 lcc 출력 830개 파일에서 `lcc-to-linalg`로 묶음이 올라간
함수 34개 중 23개에 호출이나 `llvm.load`·`llvm.store`가 있다. MLIR 23의 `affine-loop-fusion`은 이런 연산을 보지 않고, producer
모드에서도 루프 묶음을 `func.call`이나 주소가 빠져나간 전역으로의 `llvm.store` 너머로 옮긴다. 그 호출이나 저장이 같은 전역을 고치면
결과가 틀린다. 업스트림에도 비슷한 이슈가 있다(producer 루프 안의 호출
[llvm/llvm-project#211591](https://github.com/llvm/llvm-project/issues/211591), 묶음 사이의 호출이 받는 `memref.cast` 별칭
[llvm/llvm-project#211599](https://github.com/llvm/llvm-project/issues/211599)).

## 업스트림 linalg 변환과 transform

`lcc-to-linalg`의 결과에는 transform 방언 스크립트로 linalg를 타일링하고 벡터화할 수 있다([사용](#사용)). [kernels/](kernels)의 세
스크립트로 다섯 커널이 x86_64와 aarch64에서 `-O0`·`-O2` 모두 `gcc -O2`와 비트까지 같다([kernels/check.sh](#kernels)). 벡터화한
matmul 타일의 리덕션은 `vector.multi_reduction`이고, `lcc-lower`가 그것을 리덕션 차원의 순서대로 누적기에 더하므로 float도 같은
값이 된다. 마스크를 쓴 타일도 같다. `transform.structured.vectorize_children_and_apply_patterns`는 기본으로 그런 리덕션을
`vector.contract`로 바꾸는데, `convert-vector-to-llvm`은 그것을 각 내적을 +0.0부터 차례로 더한 뒤 누적기에 더하는 식으로 내린다.
정수는 같지만 float는 C 순서와 달라 `kernels/convff.c`에서 128값 중 35개가 gcc와 다르다. 순서를 지키려면
`{disable_multi_reduction_to_contract_patterns}`를 준다(그러면 같다). memref의 `linalg.conv_2d`는 MLIR 23의
`transform.structured.vectorize`가 벡터화하지 못하고 1차원 conv로 나누는 `transform.structured.decompose`도 실패해서,
스크립트들은 타일링만 한다.

lcc-opt의 패스, 위의 transform(`tile_using_for`, `vectorize`, `vectorize_children_and_apply_patterns`, transfer 치환 패턴,
canonicalize), `linalg-specialize-generic-ops`, `lcc-lower`는 연산을 다른 연산 너머로 옮기지 않는다.
`lcc-distribute-reductions`는 둘레 루프 안에서만 문장을 옮기는데, 그 안에는 affine 접근과 부작용 없는 연산뿐이다. 묶음 사이에 호출과
주소가 빠져나간 전역으로의 `llvm.store`가 있는 프로그램에서 이 변환들은 순서를 지켜 결과가 gcc와 같다. linalg의 생산자
fusion(`transform.structured.fuse`, `fuse_into_containing_op`)은 텐서 값의 생산자를 따라가므로 memref인 `lcc-to-linalg`의
결과에는 적용되지 않는다. vector transfer의 canonicalize 패턴(`TransferReadAfterWriteToBroadcast`, `FoldWaw`)은 텐서에만 쓰이고,
`vectorize_children_and_apply_patterns`의 `LinalgCopyVTRForwardingPattern`·`LinalgCopyVTWForwardingPattern`은
`memref.copy`와 `memref.alloc`·`memref.view`에만 쓰이는데 lcc-opt는 그런 연산을 만들지 않는다. 그러나 linalg를 affine 루프로 내린
뒤(`convert-linalg-to-affine-loops`)의 `affine-loop-fusion`은 `lcc-to-affine`의 결과에서와 마찬가지로 묶음을 호출과 `llvm.store`
너머로 옮겨 결과가 틀린다([업스트림 affine 변환](#업스트림-affine-변환)).

## 시험

### lit

```bash
ninja -C build-lcc-opt check-lcc-opt
```

`test/*.mlir`을 `/usr/lib/llvm-23/bin`의 `lit`과 `FileCheck`로 돌린다. lcc가 낸 입력은 만든 명령과 C 소스가 시험 파일의 주석에
있다.

| 시험 | 입력 | 보는 것 |
| :- | :- | :- |
| `prepare.mlir`, `inline.mlir` | lcc가 작은 C 함수에서 낸 `.mlir` | `lcc-prepare`가 alloca를 모두 올리고 인라인하는 것, `lcc-byval-copy`와 `lcc-hoist-allocas` |
| `memcpy.mlir`, `split.mlir` | lcc 꼴의 memcpy 호출, lcc가 구조체를 복사하는 C에서 낸 `.mlir` | 구조체 복사와 멤버를 SSA 값으로 올리기 |
| `to-scf.mlir`, `llvm-func.mlir` | lcc가 낸 루프, 가변 인자 함수와 무한 루프 | `lcc-to-scf`, `llvm.func`의 루프 |
| `rotate.mlir`, `uplift.mlir` | 손으로 쓴 `scf.while` | 각 패스가 바꾸는 경우와 그대로 두어야 하는 경우 |
| `text.mlir` | `f80`·`f128` 상수, `i64` switch | 텍스트 출력 검사와 bytecode가 값을 지키는 것 |
| `index-loops.mlir`, `globals-to-memref.mlir`, `scf-to-affine.mlir` | lcc 출력 모양으로 손으로 쓴 입력 | 바꾸는 경우와 남기는 경우, 남기는 이유(remark) |
| `to-affine.mlir` | lcc가 `convfix.c`(x86_64, armhf)와 상수 가중치 표판에서 낸 `.mlir` | `lcc-to-affine` |
| `expand-shapes.mlir`, `distribute-reductions.mlir`, `affine-to-linalg.mlir` | 손으로 쓴 affine | 바꾸는 경우와 남기는 경우 |
| `to-linalg.mlir` | lcc가 `convfix.c`(x86_64, armhf), 상수 표판, 작은 float판, 행렬 곱, 3x3 합성곱에서 낸 `.mlir` | `lcc-to-linalg` |
| `transform.mlir` | `convfix.c`의 `lcc-to-linalg` 결과와 transform 스크립트 | 타일링·벡터화와 `lcc-lower` |
| `distinct-successors.mlir` | 반복되는 후속 블록 | 그 순서대로 나누기 |
| `lower.mlir` | `lcc-to-scf`·`lcc-to-affine`·`lcc-to-linalg`가 남기는 것, 업스트림 변환 뒤의 것 | `lcc-lower`가 scf·ub·arith·cf·func, affine·memref·vector, linalg를 남기지 않는 것 |
| `pragma-attrs.mlir` | lcc가 `-Wf-pragma=knpu`로 낸 `.mlir` | `knpu.*` 함수 속성이 `lcc-to-linalg`·`lcc-lower`를 지나도 남는 것, 속성이 가리킨 전역이 그대로인 것(remark) |

### kernels

[kernels/](kernels)에 완료 기준의 커널(저장소 README의 [완료 기준과 결과](../README.md#완료-기준과-결과))과 검사 스크립트가 있다.

| 파일 | 내용 |
| :- | :- |
| `convfix.c` | 고정 크기 전역 배열의 1x1 int8 합성곱(64x16 입력, 32x16 가중치, bias, -128~127로 자르기) |
| `convw.c` | `convfix.c`를 작게, 음수가 있는 상수 가중치 표와 bias 표로 |
| `convff.c` | `convw.c`를 float로(-0.0, 비정규 수, 정확하지 않은 값, 서로 지우는 항, 곱이 넘치는 가중치) |
| `mm.c` | int 행렬 곱 셋(메모리에 누적, 지역 합, 0으로 둔 뒤 메모리에 누적) |
| `conv3.c` | 34x34 영상을 32x32로 하는 3x3 합성곱 둘(메모리에 누적, 지역 합) |
| `main.c` | 커널 하나(`-DCONVFIX` 등)의 입력을 LCG로 채워 시드 넷으로 돌리고 쓴 배열마다 체크섬을 찍는다(`-v`면 원소마다) |
| `tile.mlir`, `tile-vector.mlir`, `mask-vector.mlir` | transform 스크립트([사용](#사용)) |
| `check.sh` | 아래 검사 |

```bash
lcc-opt/kernels/check.sh [-o outdir] builddir build-lcc-opt/bin/lcc-opt x86_64-linux    # 또는 aarch64-linux
```

`builddir`은 그 타깃의 mlir 백엔드로 빌드한 lcc다(`make BUILDDIR=builddir HOSTFILE=etc/mlir-T.c rcc lcc cpp headers`). 검사는
셋이고, 하나라도 틀리면 0이 아닌 값으로 끝난다.

- IR의 모양: `convfix.c`가 `lcc-to-affine`으로 `affine.for` 셋이 되고, `affine-loop-tile=tile-sizes=8,8` 뒤 다섯이 되며,
  `affine-super-vectorize=virtual-vector-size=8 vectorize-reductions=true` 뒤 벡터 읽기와 `vector.reduction <add>`가 있다.
  다섯 커널 모두 `lcc-to-linalg`로 루프가 남지 않고 정해진 linalg 연산이 되며(`convfix.c`는 `linalg.broadcast`, `linalg.matmul`,
  parallel 차원 둘인 `linalg.generic`), 세 스크립트 뒤에 타일 루프가, 벡터화하는 두 스크립트 뒤에 vector 연산이,
  `mask-vector.mlir` 뒤에 `vector.mask`가 있다.
- 실행 결과: 커널마다 구성 여덟(lcc 드라이버 그대로, `lcc-to-affine`, 그 뒤 `affine-loop-tile`, 그 뒤 `affine-super-vectorize`,
  `lcc-to-linalg`, 그 뒤 세 스크립트 각각)으로 만든 LLVM IR을 `opt -passes=verify`로 검사하고 clang `-O0`·`-O2`로 컴파일해,
  `main.c`와 링크한 프로그램이 커널을 `gcc -O2`로 컴파일한 것과 같은 것을 찍어야 한다. gcc와 clang에는 `-ffp-contract=off`를 준다.
  `convff.c`의 affine 벡터화는 float 합의 순서가 바뀌므로 비교하지 않고 다른 값의 수와 가장 큰 상대 차이만 알리고(`NOTE`), `mm.c`의
  affine 벡터화는 업스트림 결함으로 틀리므로 돌리지 않는다([업스트림 affine 변환](#업스트림-affine-변환)).
- 결정성: 구성마다 lcc-opt와 `mlir-translate`를 두 번 돌려 LLVM IR이 같아야 한다.

aarch64-linux는 `aarch64-linux-gnu-gcc`로 컴파일하고 `qemu-aarch64 -L /usr/aarch64-linux-gnu`로 돌린다.

### CI

[CI](../.github/workflows/ci.yml)의 `lcc-opt` 작업이 x86_64-linux와 aarch64-linux에서 다음을 차례로 한다. 단계마다
[lcc-opt-check.sh](../.github/scripts/lcc-opt-check.sh)의 하위 명령이므로 로컬에서도 같은 명령으로 재현된다.
`lcc-opt-check.sh all T 작업디렉터리 시험묶음디렉터리`는 `deps` 말고 모두를 차례로 돈다.

| 하위 명령 | 하는 일 |
| :- | :- |
| `deps T` | `ci-target.sh deps T mlir`(apt.llvm.org의 LLVM 23, 교차 gcc, qemu)와 `libmlir-23-dev`, `llvm-23-dev`, `llvm-23-tools`, `libclang-rt-23-dev`, cmake, ninja를 설치하고 받은 LLVM 23 패키지의 판을 찍는다 |
| `lit T W` | `W/lcc-opt`에 lcc-opt를 빌드하고 lit 시험 |
| `sanitizers T W` | `W/lcc-opt-san`에 clang 23의 `-fsanitize=address,undefined -fno-sanitize-recover=all`로 빌드하고 lit 시험. 보고가 나면 abort하므로 lcc-opt가 실패하기를 기대하는(`not`) 시험도 실패한다 |
| `lcc T W` | `W/lcc`에 mlir 백엔드 lcc를, `W/lcc-raise`에 드라이버가 도구를 `W/bin`에서 찾는(`-DLLVMBIN`) lcc를 빌드한다. `W/bin/mlir-opt`는 드라이버가 주는 내리기 대신 lcc-opt `--lcc-to-linalg --lcc-lower --emit-bytecode`를 부르는 스크립트다. `lcc -v`로 그 lcc-opt가 불리는지 확인한다 |
| `kernels T W` | `kernels/check.sh` |
| `suite T W 인자…` | `W/lcc-raise`의 lcc로 `src/run-llvm.sh` 묶음(CI는 lcc, unit, `DRVOPT=-O2`의 unit, c-testsuite, torture, llvm-bench). `MLIRBIN`도 `W/bin`이어서 run-llvm.sh가 하는 IR 검사도 lcc-opt를 거친 LLVM IR에 한다 |

## 디렉터리

| 경로 | 내용 |
| :- | :- |
| `lcc-opt.cpp` | `main`: 업스트림 등록, 우리 등록, 텍스트로 쓸 때 `lcc-check-text`를 붙여 `MlirOptMain` |
| `include/lcc-opt/Passes.h`, `include/lcc-opt/Passes.td`, `lib/` | 우리 패스와 파이프라인(`LccOptPasses`). 패스 선언은 TableGen |
| `test/` | lit 시험 |
| `kernels/` | 완료 기준의 커널, transform 스크립트, 검사 스크립트 |

## 라이선스

lcc-opt는 Apache License v2.0 with LLVM Exceptions다([LICENSE](LICENSE)). 소스 파일마다
`SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`이 있다. 저장소의 나머지인 lcc는 [CPYRIGHT](../CPYRIGHT)를 따른다.
