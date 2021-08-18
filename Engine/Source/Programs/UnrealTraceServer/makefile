# vim: set foldlevel=1 :

# {{{1 debug and flags ---------------------------------------------------------
OUT_DIR := _build
ifeq ($(DEBUG), 1)
CFLAGS := \
	$(CFLAGS) \
	-DTS_BUILD_DEBUG=TS_ON \
	-Og
OUT_DIR := $(OUT_DIR)/debug
else
CFLAGS := \
	$(CFLAGS) \
	-O3
OUT_DIR := $(OUT_DIR)/development
endif

CFLAGS := \
	$(CFLAGS) \
	-g \
	-Werror \
	-std=c++17 \
	-fno-exceptions \
	-static

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
	LDLIBS := $(LDLIBS) -lstdc++ -lpthread -lrt
endif

ifeq ($(UNAME), Darwin)
	LDLIBS := $(LDLIBS) -lstdc++ -lpthread
endif

# {{{1 all/clean targets -------------------------------------------------------
.SILENT :

.PHONY : clean all unreal_trace

all : unreal_trace

clean :
	rm -rf $(OUT_DIR)

$(OUT_DIR) :
	mkdir -p $@

# {{{1 asio --------------------------------------------------------------------
ASIO_VER = 1.12.2
CFLAGS := \
	$(CFLAGS) \
	-I3rdparty/asio/$(ASIO_VER)

# {{{1 store compile/link ------------------------------------------------------
OBJS := \
	$(OBJS) \
	$(OUT_DIR)/Asio.o \
	$(OUT_DIR)/AsioContext.o \
	$(OUT_DIR)/AsioFile.o \
	$(OUT_DIR)/AsioIoable.o \
	$(OUT_DIR)/AsioSocket.o \
	$(OUT_DIR)/AsioTcpServer.o \
	$(OUT_DIR)/AsioTickable.o \
	$(OUT_DIR)/Cbor.o \
	$(OUT_DIR)/Main.o \
	$(OUT_DIR)/Recorder.o \
	$(OUT_DIR)/Store.o \
	$(OUT_DIR)/StoreCborServer.o \
	$(OUT_DIR)/StoreService.o \
	$(OUT_DIR)/TraceRelay.o

$(OUT_DIR)/%.o : src/%.cpp
	echo $(<F)
	$(CXX) $(CFLAGS) -c -o $@ $<

$(OUT_DIR)/UnrealTraceServer : $(OUT_DIR) $(OBJS)
	echo $(@F)
	$(CXX) -o $@ $(OBJS) $(LDLIBS)

unreal_trace : $(OUT_DIR)/UnrealTraceServer
