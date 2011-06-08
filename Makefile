CC=gcc
CFLAGS=-fdata-sections -ffunction-sections -g -c -I/usr/local/include/hiredis/
RM=rm -rf
MKDIR=mkdir -p
MAKEDEPEND=gcc -MM $(CFLAGS)
LDFLAGS=-lzmq -ldl -lrt -pthread --gc-sections -g
LDFLAGS_receiver=$(LDFLAGS) /usr/local/lib/libhiredis.a
LDFLAGS_lib=-fPIC -shared -lzmq -ldl -lrt -pthread --gc-sections -g

EXE_collect=collector
EXE_receive=receiver
LIB_tracelib=libcfstrace.so

SRC_collect=collect.c shm_mbuffer.c
SRC_receive=receiver.c sqlite3.c sqlite_adapter.c redis_adapter.c
SRC_tracelib=cfstrace.c shm_mbuffer.c

OBJ_collect=$(SRC_collect:%.c=obj/%.o)
OBJ_receive=$(SRC_receive:%.c=obj/%.o)
OBJ_tracelib=$(SRC_tracelib:%.c=obj/%.o)

obj/%.d : %.c
	@set -e; rm -f $@; mkdir -p obj; \
	$(MAKEDEPEND) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,obj/\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

all: $(SRC_collect) $(SRC_receive) $(SRC_tracelib) $(EXE_collect) $(EXE_receive) $(LIB_tracelib)

-include $(SRC_collect:%.c=obj/%.d)
-include $(SRC_receive:%.c=obj/%.d)
-include $(SRC_tracelib:%.c=obj/%.d)

$(EXE_collect): $(OBJECTS) $(OBJ_collect)
	$(CC) -o $@ $(OBJECTS) $(OBJ_collect) $(LDFLAGS)

$(EXE_receive): $(OBJECTS) $(OBJ_receive)
	$(CC) -o $@  $(OBJECTS) $(OBJ_receive) $(LDFLAGS_receiver)

$(LIB_tracelib): $(OBJECTS) $(OBJ_tracelib)
	$(CC) -o $@ $(OBJECTS) $(OBJ_tracelib) $(LDFLAGS_lib)

obj/%.o:
	$(CC) -o $@ $< $(CFLAGS)

depend:
	$(CC) -E -MM *.c > depend_all.d

clean:
	$(RM) $(EXE_collect) $(EXE_receive) $(LIB_tracelib) obj







