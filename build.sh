# !bash
ROOT_PATH=$(pwd)
sysOS=`uname -s`
mac_llvm_source="https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.0/clang+llvm-10.0.0-x86_64-apple-darwin.tar.xz"
ubuntu_llvm_source="https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.0/clang+llvm-10.0.0-x86_64-linux-gnu-ubuntu-18.04.tar.xz"
mac_z3_source="https://github.com/Z3Prover/z3/releases/download/z3-4.8.8/z3-4.8.8-x64-osx-10.14.6.zip"
ubuntu_z3_source="https://github.com/Z3Prover/z3/releases/download/z3-4.8.8/z3-4.8.8-x64-ubuntu-16.04.zip"

echo "check LLVM_DIR and Z3_PACKAGE!"
if [ $LLVM_DIR ]
then echo "LLVM exist!!!"
else
    if [ -x "LLVM_source.tar.xz" ] ; then
        if [ -d "LLVM_source" ] ; then
            rm -rf ./'LLVM_source'
            mkdir ./LLVM_source && tar -xvf "LLVM_source.tar.xz" -C ./LLVM_source --strip-components 1
            export LLVM_DIR=$ROOT_PATH"/"$LLVM_source
        else
            mkdir ./LLVM_source && tar -xvf "LLVM_source.tar.xz" -C ./LLVM_source --strip-components 1
            export LLVM_DIR=$ROOT_PATH"/"$LLVM_source
        fi
    else
        if [ $sysOS == "Darwin" ] ; then
            echo 'macOS '
            wget -c $mac_llvm_source -O LLVM_source.tar.xz
            mkdir ./LLVM_source && tar -xvf "LLVM_source.tar.xz" -C ./LLVM_source --strip-components 1
            export LLVM_DIR=$ROOT_PATH"/LLVM_source"
        elif [ $sysOS == "Linux" ] ; then
            echo 'ubuntu'
            wget -c $ubuntu_llvm_source -O LLVM_source.tar.xz
            mkdir ./LLVM_source && tar -xvf "LLVM_source.tar.xz" -C ./LLVM_source --strip-components 1
            export LLVM_DIR=$ROOT_PATH"/LLVM_source"
        fi
    fi
fi
if [ $Z3_DIR ]
then
    echo 'Z3 package exist!!!'
else
    if [ -x "Z3_source.zip" ] ; then
        if [ -d "Z3_source" ] ; then
            rm -rf ./'Z3_source'
            unzip "Z3_source.zip" && mv ./z3-* ./Z3_source
            export Z3_DIR=$ROOT_PATH"/Z3_source"
        else
            unzip "Z3_source.zip" && mv ./z3-* ./Z3_source
            export Z3_DIR=$ROOT_PATH"/Z3_source"
        fi
    else
        if [ $sysOS == "Darwin" ] ; then
            echo 'macOS '
            wget -c $mac_z3_source -O Z3_source.zip
            unzip "Z3_source.zip" && mv ./z3-* ./Z3_source
            export Z3_DIR=$ROOT_PATH"/Z3_source"
        elif [ $sysOS == "Linux" ] ; then
            echo 'ubuntu'
            wget -c $ubuntu_z3_source -O Z3_source.zip
            unzip "Z3_source.zip" && mv ./z3-* ./Z3_source
            export Z3_DIR=$ROOT_PATH"/Z3_source"
        fi
    fi
fi
#
#
. ./setup.sh $LLVM_DIR
if [ -d 'Test-Suite' ] ; then
echo 'Test-Suite exists！'
else
rm -rf ./'Test-Suite'
echo 'Download Test-Suite'
git clone "https://github.com/JasonZhongZexin/Test-Suite.git"
cd ./'Test-Suite'
./generate_bc.sh
cd ..
fi
echo "check cmake !!"
cmake --version
if [[ $1 == 'debug' ]]
then
BuildTY='Debug'
else
BuildTY='Release'
fi

Build=$BuildTY'-build'
rm -rf ./$Build
mkdir ./$Build
cd ./$Build

## start building SVF
if [[ $1 == 'debug' ]]
then
echo "check svf mode for debug"
cmake -D CMAKE_BUILD_TYPE:STRING=Debug ../
else
echo "check svf mode for release"
cmake ../
fi
make -j4

cd ../
. ./setup.sh $LLVM_DIR
cd ./$Build
ctest
