#! /bin/bash	

printf "\t=========== Create archive example contracts ===========\n\n"	

RED='\033[0;31m'	
NC='\033[0m'	

asset_dir=$(realpath "build/assets")	
mkdir -p $asset_dir

(
    set -x
    cd build/contracts
    tar -czf $asset_dir/contracts.tar.gz *.abi *.wasm
)
