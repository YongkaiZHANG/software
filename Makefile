TITLE_COLOR = \033[33m
NO_COLOR = \033[0m

PORT ?= 5691
TIMEOUT ?= 30
SET_MIN_TEMP ?= 10
SET_MAX_TEMP ?= 20
HOST ?= 127.0.0.1
ROOM ?= 1
SENSOR ?= 15
SLEEP ?= 1
LOOPS ?= 0

CPPFLAGS_COMMON = -DSET_MIN_TEMP=$(SET_MIN_TEMP) -DSET_MAX_TEMP=$(SET_MAX_TEMP) -DTIMEOUT=$(TIMEOUT) -DPORT=$(PORT)
CPPCHECK ?= cppcheck

ifneq ("$(wildcard file_creator.c)","")
ALL_FILE_CREATOR_TARGET = file_creator
endif

.DEFAULT_GOAL := all

# build binaries only
all: sensor_gateway sensor_node $(ALL_FILE_CREATOR_TARGET)

# When trying to compile one of the executables, first look for its .c files
# Then check if the libraries are in the lib folder
sensor_gateway : main.c connmgr.c datamgr.c sensor_db.c sbuffer.c lib/libdplist.so lib/libtcpsock.so
	@echo "$(TITLE_COLOR)\n***** CPPCHECK *****$(NO_COLOR)"
	@if command -v $(CPPCHECK) >/dev/null 2>&1; then \
		$(CPPCHECK) --enable=all --suppress=missingIncludeSystem main.c connmgr.c datamgr.c sensor_db.c sbuffer.c; \
	else \
		echo "cppcheck not found, skipping static analysis"; \
	fi
	@echo "$(TITLE_COLOR)\n***** COMPILING sensor_gateway *****$(NO_COLOR)"
	gcc -c main.c      -Wall -std=c11 -Werror $(CPPFLAGS_COMMON) -o main.o      -fdiagnostics-color=auto
	gcc -c connmgr.c   -Wall -std=c11 -Werror $(CPPFLAGS_COMMON) -o connmgr.o   -fdiagnostics-color=auto
	gcc -c datamgr.c   -Wall -std=c11 -Werror $(CPPFLAGS_COMMON) -o datamgr.o   -fdiagnostics-color=auto
	gcc -c sensor_db.c -Wall -std=c11 -Werror $(CPPFLAGS_COMMON) -o sensor_db.o -fdiagnostics-color=auto
	gcc -c sbuffer.c   -Wall -std=c11 -Werror $(CPPFLAGS_COMMON) -o sbuffer.o   -fdiagnostics-color=auto
	@echo "$(TITLE_COLOR)\n***** LINKING sensor_gateway *****$(NO_COLOR)"
	gcc main.o connmgr.o datamgr.o sensor_db.o sbuffer.o -ldplist -ltcpsock -lpthread -o sensor_gateway -Wall -L./lib -Wl,-rpath,./lib -lsqlite3 -fdiagnostics-color=auto

file_creator : file_creator.c
	@echo "$(TITLE_COLOR)\n***** COMPILE & LINKING file_creator *****$(NO_COLOR)"
	gcc file_creator.c -o file_creator -Wall -fdiagnostics-color=auto

sensor_node : sensor_nodes.c lib/libtcpsock.so
	@echo "$(TITLE_COLOR)\n***** COMPILING sensor_node *****$(NO_COLOR)"
	gcc -c sensor_nodes.c -Wall -std=c11 -Werror $(CPPFLAGS_COMMON) -o sensor_node.o -fdiagnostics-color=auto
	@echo "$(TITLE_COLOR)\n***** LINKING sensor_node *****$(NO_COLOR)"
	gcc sensor_node.o -ltcpsock -o sensor_node -Wall -L./lib -Wl,-rpath,./lib -fdiagnostics-color=auto

# If you only want to compile one of the libs, this target will match (e.g. make liblist)
libdplist : lib/libdplist.so
libtcpsock : lib/libtcpsock.so

lib/libdplist.so : lib/dplist.c
	@echo "$(TITLE_COLOR)\n***** COMPILING LIB dplist *****$(NO_COLOR)"
	gcc -c lib/dplist.c -Wall -std=c11 -Werror -fPIC -o lib/dplist.o -fdiagnostics-color=auto
	@echo "$(TITLE_COLOR)\n***** LINKING LIB dplist *****$(NO_COLOR)"
	gcc lib/dplist.o -o lib/libdplist.so -Wall -shared -lm -fdiagnostics-color=auto

lib/libtcpsock.so : lib/tcpsock.c
	@echo "$(TITLE_COLOR)\n***** COMPILING LIB tcpsock *****$(NO_COLOR)"
	gcc -c lib/tcpsock.c -Wall -std=c11 -Werror -fPIC -o lib/tcpsock.o -fdiagnostics-color=auto
	@echo "$(TITLE_COLOR)\n***** LINKING LIB tcpsock *****$(NO_COLOR)"
	gcc lib/tcpsock.o -o lib/libtcpsock.so -Wall -shared -lm -fdiagnostics-color=auto

# do not look for files called clean, clean-all or this will be always a target
.PHONY : all clean clean-all run run-multi zip

clean:
	rm -rf *.o sensor_gateway sensor_node main sensor_nodes file_creator *~

clean-all: clean
	rm -rf lib/*.so

run : sensor_gateway sensor_node
	@echo "$(TITLE_COLOR)\n***** RUN sensor_gateway + one sensor_node *****$(NO_COLOR)"
	@./sensor_gateway $(PORT) $(TIMEOUT) & \
	gw=$$!; \
	sleep 1; \
	./sensor_node $(ROOM) $(SENSOR) $(SLEEP) $(HOST) $(PORT) $(LOOPS); \
	wait $$gw

run-multi : sensor_gateway sensor_node
	@echo "$(TITLE_COLOR)\n***** RUN sensor_gateway + sensor_node room=1..4 *****$(NO_COLOR)"
	@./sensor_gateway $(PORT) $(TIMEOUT) & \
	gw=$$!; \
	sleep 1; \
	./sensor_node 1 15 $(SLEEP) $(HOST) $(PORT) $(LOOPS) & s1=$$!; \
	./sensor_node 2 21 $(SLEEP) $(HOST) $(PORT) $(LOOPS) & s2=$$!; \
	./sensor_node 3 37 $(SLEEP) $(HOST) $(PORT) $(LOOPS) & s3=$$!; \
	./sensor_node 4 49 $(SLEEP) $(HOST) $(PORT) $(LOOPS) & s4=$$!; \
	wait $$s1; \
	wait $$s2; \
	wait $$s3; \
	wait $$s4; \
	wait $$gw

zip:
	zip final.zip main.c connmgr.c connmgr.h datamgr.c datamgr.h sbuffer.c sbuffer.h sensor_db.c sensor_db.h config.h lib/dplist.c lib/dplist.h lib/tcpsock.c lib/tcpsock.h
