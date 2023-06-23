CC =		gcc

all: test mfs


test: main.o
	gcc -o test main.o -g --std=c99

mfs: mfs.o
	gcc -o mfs mfs.o -g --std=c99

clean:
	rm -f *.o *.a a.out test mfs

# To avoid a zero, the last test must be compiled with: 
final:
	gcc -Wall -Werror --std=c99 mfs.c

.PHONY: all clean

# In a Makefile, .PHONY is a special target that 
#	specifies a list of targets that are not 
# 	associated with files. This means that even if 
# 	there is a file with the same name as the 
# 	target, the target will still be executed.Hell
