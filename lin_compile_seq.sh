/home/oortwijn/upc2/bin/upcc main.c htable/htable.c varchain.c nodecache_empty.c localstore.c bdd2.c cache.c wstealer/wstealer_seq.c -O -pthreads=16 -network=mxm -o main_lin -I ./htable -I ./wstealer -I ./ -Wl,-Wl,-lpthread
 
