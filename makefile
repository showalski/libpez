IDIR = ./include/
ODIR=./obj
CC=gcc
CFLAGS=`pkg-config --cflags 'libprotobuf-c >= 1.0.0'` -I$(ODIR)/
LDFLAGS= `pkg-config --libs 'libprotobuf-c >= 1.0.0'` -lev -lzmq -lpthread
BUILD=build/
 
obj/%.o: src/%.c
	mkdir -p obj
	$(CC) $(CFLAGS) -I. -c $< -o $@
 
src/%.pb-c.c src/%.pb-c.h: src/%.proto
	protoc-c --c_out=. $<
 
OBJ =  $(ODIR)/msg.pb-c.o \
       $(ODIR)/main.o \
       $(ODIR)/pez_ipc.o \
       $(ODIR)/ev_zsock.o
 
main: $(OBJ)
	mkdir $(BUILD)
	gcc -o $(BUILD)/$@ $(OBJ) $(LDFLAGS)
 
.PHONY: clean all
 
all: clean  main
 
clean:
	rm -rf $(ODIR)
	rm -rf $(BUILD)
