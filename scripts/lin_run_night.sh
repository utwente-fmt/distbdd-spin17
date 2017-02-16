#!/bin/sh

RESULT_DIR=~/distdd/result

for try in 1 2 3
do
	
	while read f
	do
		
		IFS=","
		linecount=0
		while read machines threads tablesize cachesize sharedmemory otheroptions
		do
			if [ $linecount -eq 0 ]; then
				linecount=$((linecount+1));
				continue;
			fi

			FILENAME=$(basename ~/distdd/models/beem/$f)
			MODELNAME=${FILENAME%.*}
			
			# capture current time (200 = 2AM, 2200 = 10PM, etc..)
			currenttime=$(date +"%k%M")
			
			# execute body only NOT between 08:00 and 20:00
			while [ $currenttime -gt 800 -a $currenttime -lt 2000 ]
			do
			  sleep 60;
			done
			
			TOTAL=$((machines * threads));
			MODELDESC=${MODELNAME//.};
			RESULTNAME=${MODELDESC}_${machines}_${threads}"_try_"${try};

			# skip benchmark if there already is a result file
                        if [ -f $RESULT_DIR/$RESULTNAME.out ]; then
                          continue;
                        fi

			# submit the benchmark run
			echo "Submitting $MODELNAME on $machines machines with $threads theads each ($TOTAL threads in total)";
			sbatch --exclusive --time=60 --nodes=$machines --error=$RESULT_DIR/$RESULTNAME.err --output=$RESULT_DIR/$RESULTNAME.out lin_submit.sh $machines $threads $TOTAL $MODELNAME $tablesize $cachesize $sharedmemory $otheroptions;
			
			# wait until less than 3 runs are active
			while [ $(squeue -h -uoortwijn | wc -l) -gt 0 ]; do
				sleep 10;
			done
		
		done < ~/distdd/benchmarksuite_night.csv
	
	done < ~/distdd/models/beem/info/small.txt
done
