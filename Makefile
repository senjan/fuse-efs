CC=gcc
CFLAGS=-Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse

DEPS=efs_dir.h efs_file.h efs_fs.h efs_vol.h utils.h
OBJ=efs_dir.o efs_file.o efs_fs.o efs_vol.o main.o utils.o

all:	fuse-efs	

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

fuse-efs: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) fuse-efs 
