#!/bin/bash

# Expected argument list:
# $1 = number of machines
# $2 = number of threads per machine
# $3 = total number of threads (i.e. $1*$2)
# $4 = model name (e.g. sokoban.1 or anderson.6), without '.bdd' extension
# $5 = tablesize
# $6 = cachesize
# $7 = sharedmemory
# $8,$9 = optional arguments

export GASNET_BACKTRACE=1
export GASNET_MXM_SPAWNER=ssh
export GASNET_MPI_SPAWNER=mpi

export HPCX_HOME=/cm/shared/package/HPC-x/1.5.370-gcc
source $HPCX_HOME/hpcx-init.sh
hpcx_load

export UPC_ENVPREFIX=$UPC_ENVPREFIX,LD_LIBRARY_PATH

echo "performing reachability, flags: -n $3 -N $1 -shared-heap $7 --tablesize=$5 --cachesize=$6 $8 $9"

ulimit -s 10240

~/local/upc/bin/upcrun -version
~/local/upc/bin/upcrun -n $3 -c $2 -N $1 -shared-heap $7 -bind-threads -fca_enable 0  ~/distdd/main_lin ~/distdd/models/beem/$4.bdd --tablesize=$5 --cachesize=$6 $8 $9
