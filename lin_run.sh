#!/bin/bash
 
MODEL="$1.bdd"
RESULT_DIR="result2/$1_$2"

rm -r $RESULT_DIR
mkdir $RESULT_DIR

machines=( 32 )
threads=( 32 )

for n in "${machines[@]}"; do
	for t in "${threads[@]}"; do
		sleep 2

		TOTAL=$((n*t))
		echo "Submitting job on $n machines with $t workers (total nr of threads: $TOTAL)"
		sbatch --exclusive --time=60 --nodes=$n --error=$RESULT_DIR/result-$n-$t.err --output=$RESULT_DIR/result-$n-$t.out lin_submit.sh $n $t $TOTAL models/beem/$MODEL

		while [ $(squeue -h -uoortwijn | wc -l) -gt 0 ]; do
			sleep 10
		done

		echo "Done!"
		cat $RESULT_DIR/result-$n-$t.out | grep "PAR Time"
		cat $RESULT_DIR/result-$n-$t.out | grep "Final states"
	done
done

