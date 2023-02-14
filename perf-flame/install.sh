#!/bin/bash

tar_path="/usr/share"

if [ -d "$tar_path/FlameGraph-1.0" ]; then
	echo "FlameGraph is exist"
	exit 0
fi

opath=`pwd`

wget https://github.com/brendangregg/FlameGraph/archive/refs/tags/v1.0.tar.gz
tar -zxvf v1.0.tar.gz
sudo mv FlameGraph-1.0 $tar_path

cd $tar_path/FlameGraph-1.0

path=`pwd`

sudo ln -s $path/flamegraph.pl /usr/local/bin/flamegraph.pl
sudo ln -s $path/stackcollapse-perf.pl /usr/local/bin/stackcollapse-perf.pl

cd $opath
