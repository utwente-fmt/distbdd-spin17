#!/bin/bash
 
export GASNET_BACKTRACE=1
export GASNET_MXM_SPAWNER=ssh

export UPC_ENVPREFIX=$UPC_ENVPREFIX,LD_LIBRARY_PATH

~/local/upc_large/bin/upcrun -version
~/local/upc_large/bin/upcrun -n $3 -N $1 -shared-heap 1GB main_lin $4 --tablesize=24 --cachesize=23 -bind-threads
