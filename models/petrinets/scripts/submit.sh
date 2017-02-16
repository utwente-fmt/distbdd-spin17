#!/bin/bash

export PATH=$PATH:/home/oortwijn/local/ltsmin/bin
export PATH=$PATH:/home/oortwijn/local/divine2/bin
export MODELPATH=/home/oortwijn/distdd/models/petrinets/originals

#pnml2lts-sym $MODELPATH/$1 --when -rgs --vset=sylvan --sylvan-bits=$2 --sylvan-report-gc --lace-workers=16 --sylvan-tablesize=29 --sylvan-cachesize=29 --save-reachable --save-transitions=transitions/$3.bdd

pnml2lts-sym $MODELPATH/$1 --when -rf,rs,hf --order=chain --saturation=sat-like --sat-granularity=5 --vset=sylvan --sylvan-tablesize=29 --sylvan-cachesize=29 --lace-workers=16 --save-reachable --save-transitions=transitions/$3.bdd --sylvan-bits=$2
