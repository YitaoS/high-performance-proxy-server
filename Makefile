###complie option###
CC = g++
DREW_OF_THREE = -Wall -Werror -pedantic 
CFLAGS = -std=c++11 -MMD $(DREW_OF_THREE) $(MTHREAD_FLAG) $(INCLUDE_DIR)

MTHREAD_FLAG = -pthread -fsanitize=thread
# INCLUDE_DIR = -I /user/include/boost
LIBS = -lssl -lcrypto

###
all: proxy 
proxy: *.cpp
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

%.o: %.cpp %.hpp
	$(CC) $(CFLAGS) $< -o $@

-include $(wildcard *.d)

.PHNOY:
clean:
	rm -rf *~ *.o *.d proxy log.txt