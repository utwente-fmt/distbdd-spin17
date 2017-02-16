#!/bin/bash

export PATH=$PATH:/home/oortwijn/ltsmin/bin
export PATH=$PATH:/home/oortwijn/repos/divine2/bin
export MODELPATH=/home/oortwijn/distdd/models/promela/originals

prom2lts-sym $MODELPATH/$1 --when -rgs --vset=sylvan --sylvan-bits=$2 --sylvan-report-gc --lace-workers=16 --sylvan-tablesize=30 --sylvan-cachesize=30 --save-reachable --save-transitions=transitions/$3.bdd
