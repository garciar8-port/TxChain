# Makefile — TxChain (EE 450 final project)
#
#   make all    compile all source files and create executables
#   make clean  remove all executable files
#
# Targets per the project spec:
#   ./serverM  ./serverA  ./serverB  ./serverC  ./client  ./monitor

CXX      = g++
CXXFLAGS = -Wall -g

EXECS = serverM serverA serverB serverC client monitor

all: $(EXECS)

serverM: serverM.cpp
	$(CXX) $(CXXFLAGS) -o serverM serverM.cpp

serverA: serverA.cpp
	$(CXX) $(CXXFLAGS) -o serverA serverA.cpp

serverB: serverB.cpp
	$(CXX) $(CXXFLAGS) -o serverB serverB.cpp

serverC: serverC.cpp
	$(CXX) $(CXXFLAGS) -o serverC serverC.cpp

client: client.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp

monitor: monitor.cpp
	$(CXX) $(CXXFLAGS) -o monitor monitor.cpp

clean:
	rm -f $(EXECS)

.PHONY: all clean
