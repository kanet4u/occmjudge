all: occmserver.cpp
	g++ -Wall -c -I/usr/local/mysql/include/mysql -I/usr/include/mysql  occmserver.cpp
	g++ -Wall -o occmserver occmserver.o -L/usr/local/mysql/lib/mysql -L/usr/lib/mysql  -lmysqlclient
