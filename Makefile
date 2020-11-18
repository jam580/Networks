# 	 Makefile for COMP 112 Final Project
#
#    Authors: Ramon Fernandes, James Mattei
#	 Date: 11/17/2020
#
#    Maintenance targets:
#
#
#    all         - (default target) make sure everything's compiled
#    clean       - clean out all compiled object and executable files
#    server      - compile just the server program
#
#

# Executables to built using "make all"
EXECUTABLES = server

# Do all C compiles with gcc
CC = gcc

# Updating include path to use current directory
IFLAGS = -I.

# link against system software
CFLAGS =  -g -Wall -Wextra $(IFLAGS)
LIBS = -lnsl

# Linking flags, used in the linking step
# Set debugging information and update linking path
LDFLAGS = -g

# Linking libraries
LDLIBS = -lnsl
LDLIBS_LOCAL = ""

# 
#    'make all' will build all executables
#
#    Note that "all" is the default target that make will build
#    if nothing is specifically requested
#
all: $(EXECUTABLES)

# 
#    'make clean' will remove all object and executable files
#
clean:
	rm -f $(EXECUTABLES) *.o a.out

# 
#    To get any .o, compile the corresponding .c
#
%.o:%.c $(INCLUDES) 
	$(CC) $(CFLAGS) -c $<

#
# Individual executables
#
#    Each executable depends on one or more .o files.
#    Those .o files are linked together to build the corresponding
#    executable.
#
server: a1.o list.o mem.o failure.o
	$(CC) $(LDFLAGS) -o server a1.o list.o mem.o failure.o $(LDLIBS)

server_local: a1.o list.o mem.o failure.o
	$(CC) $(LDFLAGS) -o server a1.o list.o mem.o failure.o $(LDLIBS_LOCAL)
