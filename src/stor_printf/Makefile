
#platform参数集合 {x86,win}
platform ?= ERROR

# 指定平台
ifeq ($(platform), ERROR)
platform_x86 = y
platform_win32 = n
else
platform_$(platform) = y
endif

ifeq ($(platform_x86), y)
	platform = x86
	G_PLATFORM ?= -DX86
	G_FLAGS ?= -DOS_LINUX -D_GNU_SOURCE
	G_COMPILE_PREFIX ?= 
endif
ifeq ($(platform_win32), y)
	platform = win32
	G_PLATFORM ?= -DWIN32
	G_FLAGS ?= -D_GNU_SOURCE
	G_COMPILE_PREFIX ?= 
endif

G_PLATFORM ?= ERROR
ifeq ($(G_PLATFORM), ERROR)	
$(error Rules.make error!!! can not find platform "$(platform)" you select!!!)
endif

G_FLAGS += $(G_PLATFORM)

VPATH = .
SOURCES = $(foreach dir,$(VPATH),$(wildcard $(dir)/*))
OBJS = $(patsubst %.c, %.o, $(filter %.c,$(SOURCES)))
OBJS += $(patsubst %.cpp, %.o, $(filter %.cpp,$(SOURCES)))

DEPS = $(OBJS:.o=.d)

CC = $(G_COMPILE_PREFIX)gcc
CXX = $(G_COMPILE_PREFIX)g++
STRIP = $(G_COMPILE_PREFIX)strip
LINK = $(G_COMPILE_PREFIX)gcc

TARGET = test_stor_printf

LDFLAGS = -L. -lpthread #-lstdc++

INCLUDE_DIR = -I.
C_FLAGS = $(G_FLAGS)
C_FLAGS += $(INCLUDE_DIR)

all : $(TARGET)

%.d: %.c
	@echo -e "	CREATE	$(notdir $@)"	
	@set -e; rm -f $@ $@.*; \
	$(CC) -MM $(C_FLAGS) $< > $@.$$$$; \
	sed 's,\($(notdir $*)\)\.o[ :]*,$*\.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$ $@.*
	
%.d: %.cpp
	@echo -e "	CREATE	$(notdir $@)"	
	@set -e; rm -f $@ $@.*; \
	$(CXX) -MM $(C_FLAGS) $< > $@.$$$$; \
	sed 's,\($(notdir $*)\)\.o[ :]*,$*\.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$ $@.*
	
.PHONY: all clean

$(TARGET):$(OBJS)
	@echo "	LINK	$(notdir $@)"
	@$(CC) $(C_FLAGS) $^ -o $@ $(LDFLAGS)
	@echo "	STRIP	$(notdir $@)"
	@$(STRIP) $@
	
ifneq ($(MAKECMDGOALS), clean)
sinclude $(DEPS)
endif

%.o:%.c
	@echo -e "\033[35m	CC	$(notdir $@)\033[0m"
	@$(CC) $(C_FLAGS) -o $@ -c $<
	
%.o:%.cpp
	@echo -e "\033[35m	CXX	$(notdir $@)\033[0m"
	@$(CXX) $(C_FLAGS) -o $@ -c $<

clean:	
	@echo "	CLEAN	$(TARGET)"
	@-rm -f $(TARGET) $(OBJS) $(DEPS) *.d.* *.jpg *.mp4 *.bin

debug:
	@echo CC = $(CC)
	@echo STRIP = $(STRIP)
	@echo LINK = $(LINK)
	@echo G_PLATFORM = $(G_PLATFORM)
	@echo C_FLAGS = $(C_FLAGS)
	@echo TARGET = $(TARGET)
	@echo OBJS = $(OBJS)
