CC=gcc
CFLAGS=-std=gnu99 -lm -g -L/usr/lib/python2.7/ -L/usr/lib -lpthread -ldl  -lutil -lm  -lpython2.7 -Xlinker -export-dynamic -Wl,-O1 -Wl,-Bsymbolic-functions -lkatcp  -lfftw3 -I/home/muchacho/katcp_devel/katcp/ -L/home/muchacho/katcp_devel/katcp/  -I/home/muchacho/git/flight/mcp/include/ -I/home/muchacho/git/flight/common/include/ -L/home/muchacho/git/flight/common/ 

current: current.c
	$(CC) -o current current.c $(CFLAGS)
	
clean:
	rm current

