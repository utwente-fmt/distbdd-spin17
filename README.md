# DistDD: Distributed Decision Diagrams
This repository hosts the full source code of the algorithms and operations described in the paper on Distributed Decision Diagrams. The models used for benchmarking can be found in the `models` folder (BEEM models, Promela models, and Petri nets). The raw uncompressed data output from the benchmark runs on the DAS-5 cluster can be found in the `data` folder. The `scripts` folder contains batch scripts used for compiling and automated benchmarking.

The main author of the DistDD paper can be contacted via w.h.m.oortwijn@utwente.nl. 

Prerequisites
---
For best performance we recommend using an Infiniband network that supports Remote Direct Memory Access (RDMA), as the algorithms are specifically designed to target RDMA. Nonetheless, also standard Ethernet networks and SMP clusters are supported. Furthermore, DistBDD has the following requirements:
- Berkeley UPC, version 2.22.3: http://upc.lbl.gov/
- The GNU Compiler Collection (GCC)
- POPT, at least version 1.10.0: http://rpm5.org/files/popt/. If you are using Cygwin with the developers tools installed, then POPT is already installed.

Configuring Berkeley UPC
---
DistDD requires UPC to be configured to handle large amounts of memory. A standard configuration would also work, but then the sizes of the shared data structures are limited to only a few megabytes. Configure UPC with the following options:
- `./configure --disable-aligned-segments --enable-allow-gcc4 --enable-sptr-struct --enable-pshm --disable-pshm-posix --enable-pshm-sysv --prefix=<path>`

If you are using Cygwin, then using SystemV shared memory may cause problems. This means that UPC must be configured with inter-process shared memory:
- `./configure --disable-aligned-segments --enable-allow-gcc4 --enable-sptr-struct --prefix=<path>`

Cross-compiling UPC (v2.22.3) for Intel MIC's
---
The following configuration is used to run UPC programs on Xeon Phi's (Intel MIC's). I'm still struggling with this configuration, and it never actually worked, so let me know if you got it right!

1. First extract the contents of the Berkeley UPC tar file: `tar -xvf <filename>` and `cd` into the new directory
2. Symlink the cross configure script into the top-level source directory: `ln -s gasnet/other/contrib/cross-configure-intel-knc .`
3. Open the cross configure script and update the compiler paths (usually the defaults are correct)
4. Create a build directory: `mkdir build` and cd into it: `cd build`
5. Make sure that `icc`, `icpc`, and `mpiicc` are in your `$PATH`
6. Configure Berkeley UPC: `../cross-configure-intel-knc --prefix=<path>`
7. Build as usual: first `make`, then `make install`

Compiling DistDD for desktop use
---
DistDD can be compiled for desktop use, i.e. for non-distributed, parallel-only use. First ensure that `upcc` and `upcrun` are in your `$PATH`, possibly via: `export PATH=$PATH:/upc/bin`. Secondly, execute the compile script: `./compile.sh`.


Running DistDD on desktops
---
The following command is used to perform symbolic reachability on the `adding.2.bdd` model by using 4 threads:
- `./run.sh 4 models/beem/adding.2.bdd`

The `/models` folder contains BDD files for BEEM models, Promela models, and Petrinets. Reachability analysis can be performed over all files with a `.bdd` extension. The `run.sh` script itself contains some options on memory usage, hash table size, etc. By default, 750MB of shared memory is used, per thread! So by using 4 threads, a total of 4*750MB of memory is used. Therefore, we advice to change these parameters when performing reachability on machines with many threads but limited memory.
