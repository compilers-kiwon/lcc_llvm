# lcc-opt

lcc의 MLIR 백엔드(`-target=mlir/T`)가 낸 `.mlir`을 업스트림 MLIR의 최적화(affine, linalg 등)가 다룰 수 있는
모양으로 올리는 도구다. MLIR 23으로 만든 `mlir-opt`이며, `MlirOptMain` 위에 업스트림의 방언·확장·패스를
모두 등록하고 lcc-opt의 패스와 파이프라인을 더한다. 지금은 첫 단계인 파이프라인 `lcc-prepare`만 있다.

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

`test/*.mlir`을 `/usr/lib/llvm-23/bin`의 `lit`과 `FileCheck`로 돌린다. 시험 입력은 lcc가 작은 C 함수에서 낸
`.mlir`이고, 만든 명령은 시험 파일의 주석에 있다.

## 사용

```bash
LCCDIR=$B $B/lcc -S foo.c                        # mlir/x86_64-linux로 빌드한 lcc: foo.mlir
build-lcc-opt/bin/lcc-opt --lcc-prepare foo.mlir
```

업스트림 패스는 `mlir-opt`과 같은 이름으로 쓴다(`--lift-cf-to-scf`, `--affine-loop-tile` 등). 목록은
`lcc-opt --help`에 있다. `mlir-opt`과 달리 업스트림의 시험용 방언과 패스(`--test-*` 등)는 없다.

## 파이프라인

| 이름 | 패스 | 하는 일 |
| :- | :- | :- |
| `lcc-prepare` | mem2reg, sroa, canonicalize, inline, mem2reg, sroa, canonicalize, cse | lcc가 지역 변수와 매개변수마다 만든 `llvm.alloca`를 SSA 값으로 바꾸고 호출을 인라인한다 |

`lcc-prepare`의 출력은 `mlir-opt --mem2reg --sroa --canonicalize --inline --mem2reg --sroa --canonicalize --cse`와
같다. inline보다 mem2reg를 먼저 하는 것은, 인라인된 함수의 `llvm.alloca`가 호출이 있던 블록(흔히 루프 본문)에
들어가는데 mem2reg가 진입 블록 밖의 `llvm.alloca`는 올리지 않기 때문이다. 모든 alloca가 없어지지는 않는다.
`tst/*.c`에서는 배열·구조체 지역 변수 등의 alloca가 남았다.

## 디렉터리

| 경로 | 내용 |
| :- | :- |
| `lcc-opt.cpp` | `main`: 업스트림 등록, 우리 등록, `MlirOptMain` |
| `include/lcc-opt/Passes.h`, `lib/` | 우리 패스와 파이프라인(`LccOptPasses`) |
| `test/` | lit 시험 |

## 라이선스

lcc-opt는 Apache License v2.0 with LLVM Exceptions다([LICENSE](LICENSE)). 소스 파일마다
`SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`이 있다. 저장소의 나머지인 lcc는
[CPYRIGHT](../CPYRIGHT)를 따른다.
