export HPCX_HOME=/cm/shared/package/HPC-x/1.5.370-gcc

source $HPCX_HOME/hpcx-init.sh
hpcx_load
env | grep HPCX
