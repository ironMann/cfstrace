CC=gcc
CFLAGS=-fdata-sections -ffunction-sections -g -c -O3
RM=rm -rf
MKDIR=mkdir -p
MAKEDEPEND=gcc -MM $(CFLAGS)
LDFLAGS=-lzmq -ldl -lrt -pthread --gc-sections
LDFLAGS_lib=-fPIC -shared -lzmq -ldl -lrt -pthread --gc-sections

EXE_collect=collector
EXE_receive=receiver
LIB_tracelib=libcfstrace.so

SRC_collect=collect.c shm_mbuffer.c
SRC_receive=receiver.c sqlite3.c sqlite_adapter.c
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
	$(CC) $(LDFLAGS) $(OBJECTS) $(OBJ_collect) -o $@

$(EXE_receive): $(OBJECTS) $(OBJ_receive)
	$(CC) $(LDFLAGS) $(OBJECTS) $(OBJ_receive) -o $@

$(LIB_tracelib): $(OBJECTS) $(OBJ_tracelib)
	$(CC) $(LDFLAGS_lib) $(OBJECTS) $(OBJ_tracelib) -o $@

obj/%.o:
	$(CC) $(CFLAGS) $< -o $@

depend:
	$(CC) -E -MM *.c > depend_all.d

clean:
	$(RM) $(EXE_collect) $(EXE_receive) $(LIB_tracelib) obj







