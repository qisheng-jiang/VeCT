
#!/bin/sh
LLVM_VERSION=${LLVM_VERSION:-9}
export LLVM_SRC=`pwd`/llvm-${LLVM_VERSION}
export LLVM_OBJ=$LLVM_SRC/llvm-objects
export LLVM_DIR=$LLVM_OBJ
export SVF_HOME=`pwd`/SVF
export PATH=$LLVM_DIR/bin:$SVF_HOME/Debug-build/bin:$PATH
