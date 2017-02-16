MODELPATH=/home/oortwijn/distdd/models/petrinets/originals
COUNT=1

for bits in 4 5 6 7 8 9 10 12 14 16 18 20
do
  	for f in $MODELPATH/*.pnml
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

                        sbatch --exclusive --time=90 --nodes=1 --error=output/$MODELNAME.err --output=output/$MODELNAME.out submit.sh $FILENAME $bits $MODELNAME
                                                
                        while [ $(squeue -h -uoortwijn | wc -l) -gt 4 ]; do
                          sleep 5
                        done
                fi
        done
done
