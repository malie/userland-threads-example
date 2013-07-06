userland-threads-example
========================

an example of managing userland threads

compile and run with

    gcc -Wall -g -O0 threads.c -o t && ./t | less

benchmark with

    gcc -Wall -g -O0 threads.c -o t -DBENCH && time ./t

tested with gcc 4.7.2
