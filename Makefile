TARGETS = fserver flb bcoordinator btable smtp_server testclient 

all: $(TARGETS)

CFLAGS ?= -I/opt/homebrew/Cellar/openssl@3/3.4.0/include/ -w

fserver:
	g++ -std=c++11 -pthread -Wall -Ifrontend/include/ -g frontend/src/*.cc -lcrypto -lresolv frontend/fserver.cc -o fserver $(CFLAGS)

flb:
	g++ -std=c++11 -pthread -Wall -Ifrontend/include/ -g frontend/src/*.cc -lcrypto -lresolv frontend/loadbalancer.cc -o flb $(CFLAGS)

smtp_server:
	g++ -std=c++11 -pthread -Wall -Ifrontend/include/ -g frontend/src/*.cc -lcrypto -lresolv frontend/smtp_server.cc -o smtp_server $(CFLAGS)

bcoordinator:
	g++ -std=c++17 -pthread -Wall -Ibackend/include/ -g backend/src/*.cpp backend/coordinator.cpp -o bcoordinator $(CFLAGS)

testclient:
	g++ -std=c++17 -pthread -Wall -Ibackend/include/ -g backend/src/*.cpp backend/test_client.cpp -o testclient $(CFLAGS)

btable:
	g++ -std=c++17 -pthread -Wall -Ibackend/include/ -g backend/src/*.cpp backend/table.cpp -o btable $(CFLAGS)

backend_src:
	g++ -std=c++17 -pthread -Wall -Ibackend/include/ -g -c backend/src/*.cpp $(CFLAGS)

pack:
	rm -f proj.zip
	zip -r proj.zip frontend README Makefile

clean:
	rm -fv $(TARGETS) *~ ; rm -fv *.o
	rm -f *server_log*; rm -f *partition*; rm -f *tablet* 
	> tpc_log1.txt
	> tpc_log2.txt
	> tpc_log3.txt
	> tpc_log4.txt
	> tpc_log5.txt
	> tpc_log6.txt

realclean:: clean
	rm -fv fserver.zip
