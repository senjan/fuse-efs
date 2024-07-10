CC=gcc
CFLAGS=-Wall -g3 -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse -g3

all:	fuse-efs	

main.o: efs_file.h efs_fs.h efs_vol.h utils.h main.c
	$(CC) $(CFLAGS) -c main.c

utils.o: utils.h utils.c
	$(CC) $(CFLAGS) -c utils.c

efs_vol.o: efs_vol.h utils.h efs_vol.c
	$(CC) $(CFLAGS) -c efs_vol.c

efs_file.o: efs_file.h utils.h efs_file.c
	$(CC) $(CFLAGS) -c efs_file.c

efs_fs.o: efs_fs.h utils.h efs_fs.c
	$(CC) $(CFLAGS) -c efs_fs.c

efs_dir.o: efs_fs.h utils.h efs_dir.c
	$(CC) $(CFLAGS) -c efs_dir.c

fuse-efs: main.o utils.o efs_vol.o efs_fs.o efs_file.o efs_dir.o
	$(CC) -o fuse-efs main.o utils.o efs_vol.o efs_fs.o efs_file.o efs_dir.o \
	    $(LDFLAGS)

clean:
	rm -f *.o fuse-efs 
