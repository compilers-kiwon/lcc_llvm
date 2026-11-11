# meta-lcc를 처음부터 빌드하기

WSL2의 Ubuntu 24.04(코어 24개, 메모리 15GB)에서 실제로 실행한 절차다.

## 1. 호스트 패키지

```bash
sudo apt install -y git python3 gawk wget diffstat chrpath cpio zstd lz4 file \
                    rpcsvc-proto python3-websockets
```

`rpcsvc-proto`는 `rpcgen`을 준다. BitBake는 레이어를 읽기도 전에 이것을
요구한다. `python3-websockets`가 있어야 5단계의 sstate 미러가 쓰는 해시
동등성 서버에 접속할 수 있다.

8단계의 확인까지 하려면 다음도 설치한다.

```bash
sudo apt install -y qemu-user clang gcc llvm
```

## 2. 로케일

BitBake는 `en_US.UTF-8`이 없으면 시작하지 않는다.

```bash
sudo sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
sudo locale-gen
```

## 3. BitBake, openembedded-core, lcc

BitBake와 openembedded-core는 따로 된 저장소다(poky의 master는 더 이상
갱신되지 않는다). 레이어는 lcc 저장소 안에 있다.

```bash
mkdir -p ~/yocto/oe && cd ~/yocto/oe
git clone --depth 1 https://git.openembedded.org/bitbake
git clone --depth 1 https://git.openembedded.org/openembedded-core
git clone https://github.com/compilers-kiwon/lcc_llvm.git
```

## 4. 빌드 디렉터리

```bash
cd ~/yocto/oe
export BITBAKEDIR=$HOME/yocto/oe/bitbake
source openembedded-core/oe-init-build-env build
```

## 5. 설정

`conf/local.conf` 끝에 더한다.

```
MACHINE = "qemux86-64"

BB_NUMBER_THREADS = "4"
PARALLEL_MAKE = "-j 6"

# 해시가 맞으면 Yocto 프로젝트가 만들어 둔 작업 결과를 받아 쓴다
SSTATE_MIRRORS ?= "file://.* http://sstate.yoctoproject.org/all/PATH;downloadfilename=PATH"
BB_HASHSERVE_UPSTREAM = "wss://hashserv.yoctoproject.org/ws"

# liblcc는 lcc의 코드다. README.md의 라이선스를 보라
LICENSE_FLAGS_ACCEPTED += "commercial_lcc-runtime"

# Yocto가 시험한 배포판 목록에 없을 때만
SANITY_TESTED_DISTROS = ""
```

레시피 `BB_NUMBER_THREADS`개가 동시에 돌고, 각각 컴파일러를 최대
`PARALLEL_MAKE`개 띄운다. 둘은 메모리에 맞춰 정한다. 15GB에는 4와
`-j 6`이 맞는다.

MLIR 백엔드(`${TARGET_PREFIX}lcc-mlir`)도 쓰려면 다음을 더한다. `mlir-native`가 MLIR을 빌드하며,
이 레시피만 병렬도를 낮춘다. 15GB에서 `-j 3`으로 30분쯤 걸렸다.

```
LCC_BACKENDS = "llvm mlir"
PARALLEL_MAKE:pn-mlir-native = "-j 3"
```

## 6. 레이어 추가와 빌드

```bash
bitbake-layers add-layer ~/yocto/oe/lcc_llvm/meta-lcc
bitbake lcc-hello
```

처음 빌드할 때는 교차 툴체인, glibc, `llvm-native`(`llc`용)를 만든다.
sstate 미러가 있으면 그중 일부는 완성된 것을 받아 온다. lcc 레시피
자체는 1분쯤 걸린다.

## 7. 결과 확인

```bash
W=tmp/work/x86-64-v3-oe-linux/lcc-hello/1.0
file $W/build/lcc-hello
echo "270 192" | qemu-x86_64 -L $W/recipe-sysroot \
    -E LD_LIBRARY_PATH=/usr/lib:/lib $W/build/lcc-hello
```

`gcd 6`을 출력한다. 이 프로그램은 타깃용이다. `qemux86-64`라도 로더와
glibc가 호스트가 아니라 sysroot에 있다. `LD_LIBRARY_PATH`는 타깃 로더가
호스트의 `ld.so.cache`를 읽지 않게 한다. 패키지는 `tmp/deploy/ipk/`에
있다.

이미지에 넣으려면 `local.conf`에 `IMAGE_INSTALL:append = " lcc-hello"`를
더하고 `core-image-minimal`을 빌드한다.

## 8. 모든 머신에서 lcc 확인

```bash
~/yocto/oe/lcc_llvm/meta-lcc/scripts/check-tests.sh \
    qemux86-64 qemuarm64 qemuriscv64 qemuarm qemuriscv32
```

머신마다 `lcc-hello`를 빌드해 실행한 뒤, 그 머신의 lcc로 저장소의
`src/run-llvm.sh`를 돌린다. 묶음은 `lcc`(lcc 자체의 `tst/*.c`)와
`unit`(`tst/llvm`; 드라이버는 툴체인의 gcc가 컴파일)이다. MLIR 백엔드를 켰으면
`lcc-mlir`로도 같은 묶음을 돌린다. 머신마다 교차
툴체인과 glibc를 새로 빌드한다. 외부 묶음은 체크아웃한 위치를 준다.

```bash
SUITES="lcc unit c-testsuite torture llvm-bench" \
C_TESTSUITE=/path/to/c-testsuite \
TORTURE=/path/to/llvm-test-suite/SingleSource/Regression/C/gcc-c-torture/execute \
LLVM_TESTSUITE=/path/to/llvm-test-suite \
    ~/yocto/oe/lcc_llvm/meta-lcc/scripts/check-tests.sh qemux86-64
```

결과는 `tmp/lcc-tests/MACHINE/`에 있다(`lcc-mlir`는 `mlir-` 접두사).

## 리비전 올리기

`lcc-cross`와 `lcc-runtime`은 `lcc.inc`의 `SRCREV` 커밋을 받는다. 이것을
push한 커밋으로 옮긴 뒤 8단계를 다시 돌린다. 작업 트리를 그대로
빌드하려면 다음을 쓴다.

```
INHERIT += "externalsrc"
EXTERNALSRC_SYMLINKS = ""
EXTERNALSRC:pn-lcc-cross-x86_64 = "/path/to/lcc_llvm"
```

아키텍처마다 `EXTERNALSRC:pn-lcc-cross-ARCH` 줄을 하나씩 둔다.
`lib/`를 바꾸지 않았다면 `lcc-runtime`은 넣지 않는다. 타깃 레시피를
패키징할 때 소스를 디버그 패키지로 하드 링크하고 권한을 바꾸는데,
외부 트리에서는 그것이 작업 트리의 파일 자체이기 때문이다.
