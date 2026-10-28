# Standalone unit test for the read-only SQLite VFS (src/infra/SqliteVfs.cpp),
# over a POSIX file backend. No PlatformIO or harness needed.
#
#   make test       # builds and runs ./sqlite-test
#
# Needs: a C++17 compiler, GNU make, and python3 (its sqlite3 module writes the
# fixture database, since the badge VFS is read-only). Runs under ASan/UBSan.
FW    := ../..
SQL   := $(FW)/lib/sqlite3/src
BUILD := build

CXX      ?= g++
CC       ?= cc
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra $(SAN) -I$(SQL) -I$(FW)/src/infra
# Keep this list in step with lib/sqlite3/library.json.
SQLFLAGS := -DSQLITE_OS_OTHER=1 -DSQLITE_THREADSAFE=0 -DSQLITE_TEMP_STORE=3 \
            -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DEFAULT_CACHE_SIZE=-64 \
            -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_DQS=0 -DSQLITE_OMIT_LOAD_EXTENSION \
            -DSQLITE_OMIT_WAL -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_PROGRESS_CALLBACK \
            -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_OMIT_AUTHORIZATION -DSQLITE_OMIT_TRACE
CFLAGS   := -O1 -g -w $(SAN) $(SQLFLAGS) -I$(SQL)
LDLIBS   := -lm

.PHONY: test clean

test: sqlite-test $(BUILD)/fixture/fixture.db
	./sqlite-test $(BUILD)/fixture/fixture.db

sqlite-test: $(BUILD)/test.o $(BUILD)/PosixBackend.o $(BUILD)/SqliteVfs.o $(BUILD)/sqlite3.o
	$(CXX) $(SAN) -o $@ $^ $(LDLIBS)

$(BUILD)/fixture/fixture.db: make_fixture.py | $(BUILD)
	mkdir -p $(dir $@)
	rm -f $@
	python3 make_fixture.py $@

$(BUILD)/%.o: %.cpp $(FW)/src/infra/SqliteVfs.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/SqliteVfs.o: $(FW)/src/infra/SqliteVfs.cpp $(FW)/src/infra/SqliteVfs.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/sqlite3.o: $(SQL)/sqlite3.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) sqlite-test
