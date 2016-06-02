VERSION = 3.02
CC = gcc
OBJ = logger.o pagefile.o mmanage.o
OBJ2 =  vmaccess.o vmappl.o
  # compiler flags:
  #  -g    adds debugging information to the executable file
  #  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall
LDFLAGS = -lpthread
BIN_APPL = vmappl
BIN_MMAN = mmanage
VMEM_PAGESIZE = 8

default: all

all: vmappl mmanage 
vmappl:  $(OBJ2) 
	$(CC) $(CFLAGS) -D VMEM_PAGESIZE=$(VMEM_PAGESIZE) -o vmappl $(OBJ2) $(LDFLAGS)

mmanage: $(OBJ)
	$(CC) $(CFLAGS) -D VMEM_PAGESIZE=$(VMEM_PAGESIZE) -o mmanage $(OBJ) $(LDFLAGS)

#gleiche Regeln für alle logger.o pagefile.o mmanage.o
%.o: %.c 
	$(CC) $(CFLAGS) -D VMEM_PAGESIZE=$(VMEM_PAGESIZE) -c -o $@ $<

clean:
	rm -rf $(BIN_MMAN) $(BIN_APPL) $(OBJ) $(OBJ2)
