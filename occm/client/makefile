all: occmclient.cpp
	g++ -Wall -c -I/usr/local/mysql/include/mysql -I/usr/include/mysql occmclient.cpp
	g++ -Wall -o occmclient occmclient.o -L/usr/local/mysql/lib/mysql -L/usr/lib/mysql -lmysqlclient
