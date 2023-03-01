###complie option###
CC = g++
DREW_OF_THREE = -Wall -Werror -pedantic 
CFLAGS = -std=c++11 -MMD $(DREW_OF_THREE) $(MTHREAD_FLAG) # $(INCLUDE_DIR)

MTHREAD_FLAG = -pthread -fsanitize=thread
# INCLUDE_DIR = -I /user/include/boost
LIBS = -lssl -lcrypto

###
all: proxy 
proxy: proxy.o proxy_server.o session.o cache_handler.o http_parser.o cache.o log_writer.o
	$(CC) $(CFLAGS) $^ -o $@ 

proxy.o:proxy.cpp proxy_server.hpp
	$(CC) $(CFLAGS) -c $< -o $@ 

proxy_server.o:proxy_server.cpp proxy_server.hpp session.hpp
	$(CC) $(CFLAGS) -c $< -o $@

session.o:session.cpp session.hpp cache_handler.hpp http_parser.hpp cache.hpp log_writer.hpp
	$(CC) $(CFLAGS) -c $< -o $@

cache_handler.o:cache_handler.cpp cache_handler.hpp cache.hpp log_writer.hpp
	$(CC) $(CFLAGS) -c $< -o $@

http_parser.o:http_parser.cpp http_parser.hpp cache.hpp
	$(CC) $(CFLAGS) -c $< -o $@

cache.o:cache.cpp cache.hpp
	$(CC) $(CFLAGS) -c $< -o $@

log_writer.o:log_writer.cpp log_writer.hpp
	$(CC) $(CFLAGS) -c $< -o $@

-include $(wildcard *.d)

.PHONY:
clean:
	rm -rf *~ *.o *.d proxy 