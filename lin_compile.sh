#!/bin/bash

~/local/upc_large/bin/upcc main.c htable.c varchain.c nodecache.c localstore.c bdd.c cache.c wstealer.c -O -network=mxm -o main_lin -lpthread -lpopt
