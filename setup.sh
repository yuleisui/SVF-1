echo "Setting up environment for SVF"

export LLVM_DIR=$1
export PATH=$LLVM_DIR/bin:$PATH
export LLVMOPT=$LLVM_DIR/bin/opt
export CLANG=$LLVM_DIR/bin/clang
export CLANGCPP=$LLVM_DIR/bin/clang++
export LLVMDIS=$LLVM_DIR/bin/llvm-dis
export LLVMLLC=$LLVM_DIR/bin/llc


