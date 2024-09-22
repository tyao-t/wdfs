# make clean all --- cleans and produces libwdfs.a wdfs_server wdfs_client
# make zip --- cleans and produces a zip file

# Add files you want to go into your client library here.
wDFS_CLI_FILES= wdfs_client.cpp 
wDFS_CLI_OBJS= wdfs_client.o

# Add files you want to go into your server here.
wDFS_SERVER_FILES = wdfs_server.cpp rw_lock.cpp
wDFS_SERVER_OBJS = wdfs_server.o rw_lock.o
# E.g. for A3 add rw_lock.c and rw_lock.o to the
# wDFS_SERVER_FILES and wDFS_SERVER_OBJS respectively.

CXX = g++

# Add the required fuse library includes.
CXXFLAGS += $(shell pkg-config --cflags fuse)
CXXFLAGS += -g -Wall -std=c++1y -MMD
# If you want to disable logging messages from DLOG, uncomment the next line.
#CXXFLAGS += -DNDEBUG

# Add fuse libraries.
LDFLAGS += $(shell pkg-config --libs fuse)

# Dependencies for the client executable.
wDFS_CLIENT_LIBS = libwdfsmain.a libwdfs.a librpc.a

OBJECTS = $(wDFS_SERVER_OBJS) $(wDFS_CLI_OBJS)
DEPENDS = $(OBJECTS:.o=.d)

# targets
.DEFAULT_GOAL = default_goal

default_goal: libwdfs.a wdfs_server

# By default make libwdfs.a and wdfs_server.
all: libwdfs.a wdfs_server wdfs_client

# This compiles object files, by default it looks for .c files
# so you may want to change this depending on your file naming scheme.
%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(LDFLAGS) -L. -lrpc $<

# Make the client library.
libwdfs.a: $(wDFS_CLI_OBJS)
	ar rc $@ $^

# Make the server executable.
wdfs_server: $(wDFS_SERVER_OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -L. -lrpc -o $@

# Make the client executable.
wdfs_client: $(wDFS_CLIENT_LIBS)
	$(CXX) $(CXXFLAGS) -o wdfs_client -L. -lwdfsmain -lwdfs -lrpc $(LDFLAGS)

# Add dependencies so object files are tracked in the correct order.
depend:
	makedepend -f- -- $(CXXFLAGS) -- $(wDFS_SERVER_FILES) $(wDFS_CLI_FILES) > .depend

-include $(DEPENDS)

# Clean up extra dependencies and objects.
clean:
	/bin/rm -f $(DEPENDS) $(OBJECTS) wdfs_server libwdfs.a wdfs_client *.log

zip: clean createzip

# Update as required.
createzip:
	zip -r wdfs.zip $(wDFS_SERVER_FILES) $(wDFS_CLI_FILES) Makefile *.h
