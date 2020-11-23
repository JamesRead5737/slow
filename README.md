# slow
Download urls with epoll and libcurl

Requirments:

sudo apt-get install libc-ares-dev

sudo apt-get install libhiredis-dev

sudo apt-get install redis-server

sudo apt-get install libcurl4-openssl-dev

Compile:

gcc -o async_dns async_dns.c -lcares -lhiredis

gcc -o slow slow.c -lhiredis -lcurl -lssl

Run:

redis-server &

./async_dns com.lowercase &

./slow
