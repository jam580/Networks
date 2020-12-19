# 	 Makefile for COMP 112 Final Project
#
#    Authors: Ramon Fernandes, James Mattei
#	 Date: 12/11/2020
#
#    Maintenance targets:
#
#
#    all         - (default target) make sure everything's compiled
#    clean       - clean out all compiled object and executable files
#    server      - compile just the server program
#
#

###############################################################################
#
#        Tufts University
#        COMP 112 Final Project
#        Authors: Ramon Fernandes, James Mattei
#        December 11, 2020
#                               
#        Maintenance targets:
#		 	all     - (default target) make sure everything's compiled
#			clean	- clean out all compiled object and executable files
#			server	- compile just the server program
#
###############################################################################

############## Variables ###############

# Executables to built using "make all"
EXECUTABLES = server

# Do all C compiles with gcc
CC = gcc

# Updating include path to use current directory
IFLAGS = -I/usr/include/python3.8

# link against system software
CFLAGS =  -g -Wall -Wextra $(IFLAGS) $(LIBS)
LIBS = -lnsl -lpthread -lpython3.8

# Linking flags, used in the linking step
# Set debugging information and update linking path
LDFLAGS = -g

# Linking libraries
LDLIBS = -lnsl -lpthread -lpython3.8

############### Targets ###############

all: $(EXECUTABLES) $(LIBS)

clean:
	rm -f $(EXECUTABLES) *.o a.out

# Compile step (.c files -> .o files)
# To get *any* .o file, compile its .c file with the following rule.
%.o:%.c $(INCLUDES) 
	$(CC) $(CFLAGS) -c $<

# Linking ste (.o -> executable program)
server_local: a1.o list.o mem.o failure.o clientlist.o clientinfo.o headerfieldslist.o socketconn.o atom.o table.o cache.o
	$(CC) $(LDFLAGS) $^ -o server $(LDLIBS_LOCAL)

server: server.o list.o mem.o failure.o clientlist.o clientinfo.o headerfieldslist.o socketconn.o atom.o table.o cache.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test_clientlist: clientlist_test.o clientlist.o clientinfo.o list.o mem.o failure.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)
