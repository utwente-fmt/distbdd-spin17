MODELPATH=/home/oortwijn/distdd/models/promela/originals
COUNT=1

for bits in 3 4 5 6 7 8 9 10 11 12 16
do
	for f in $MODELPATH/*.pml $MODELPATH/*.prm
	do
	  FILENAME=$(basename $f)
	  MODELNAME=${FILENAME%.*}
		
		if [ ! -f output/$MODELNAME.err ];
		then
			echo "" > output/$MODELNAME.out;
			echo "error" > output/$MODELNAME.err;
		fi
		
		if grep -q "error" output/$MODELNAME.err; 
		then
			rm output/$MODELNAME.bits;
			echo "using $bits bits per integer" > output/$MODELNAME.bits;
			echo "submitting '$MODELNAME' with $bits bits per integer";
			
			sbatch --exclusive --time=60 --nodes=1 --error=output/$MODELNAME.err --output=output/$MODELNAME.out submit.sh $FILENAME $bits $MODELNAME
			
			if (($COUNT % 5 == 0))
			then
			  while [ $(squeue -h -uoortwijn | wc -l) -gt 0 ]; do
			    sleep 5
			  done
			fi
		
			COUNT=$((COUNT+1));
		fi
	done
done
