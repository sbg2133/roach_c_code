CC=gcc
CFLAGS=-std=gnu99 -lm -g -L/usr/lib/python2.7/ -L/usr/lib -lpthread -ldl  -lutil -lm  -lpython2.7 -Xlinker -export-dynamic -Wl,-O1 -Wl,-Bsymbolic-functions -lkatcp  -lfftw3 -I~/katcp_devel/katcp/ -L~/katcp_devel/katcp  -I~/git/flight/mcp/include/ -I~/git/flight/common/include/ -L~/git/flight/common/ 

current: rome.c
	$(CC) -o rome rome.c $(CFLAGS)
	
clean:
	rm rome

