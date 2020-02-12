QUICKJS_FOLDER=./quickjs
EXAMPLES=example/async example/classes example/closures example/exception example/simple

.PHONY: all
all: $(EXAMPLES)

.PHONY: clean
clean:
	rm -f $(wildcard $(EXAMPLES)) gtest/tests

example/async:
	g++ -pthread -g -O0 -I$(QUICKJS_FOLDER) -I. --std=c++11 -Wall -o $@ $@.cpp -lboost_system -L$(QUICKJS_FOLDER) -lquickjs

example/classes:
	g++ -pthread -g -O0 -I$(QUICKJS_FOLDER) -I. --std=c++11 -Wall -o $@ $@.cpp -L$(QUICKJS_FOLDER) -lquickjs

example/closures:
	g++ -pthread -g -O0 -I$(QUICKJS_FOLDER) -I. --std=c++11 -Wall -o $@ $@.cpp -L$(QUICKJS_FOLDER) -lquickjs

example/exception:
	g++ -pthread -g -O0 -I$(QUICKJS_FOLDER) -I. --std=c++11 -Wall -o $@ $@.cpp -L$(QUICKJS_FOLDER) -lquickjs

example/simple:
	g++ -pthread -g -O0 -I$(QUICKJS_FOLDER) -I. --std=c++11 -Wall -o $@ $@.cpp -L$(QUICKJS_FOLDER) -lquickjs

.PHONY: test
test: gtest/tests

gtest/tests:
	g++ -pthread -g -O0 -I$(QUICKJS_FOLDER) -I. --std=c++11 -Wall -o $@ $@.cpp -lgtest -lgtest_main -L$(QUICKJS_FOLDER) -lquickjs

.PHONY: test-run
test-run: gtest/tests
	./gtest/tests

.PHONY: test-run-gdb
test-run-gdb: gtest/tests
	gdb --args ./gtest/tests
