# 编译器选项
CC := gcc
LIBS = -lpthread -lrt            #动态库
# 目标文件
TARGET := cmd_svr
TARGET2 := cmd_cli

# 静态库名称
STATIC_LIB_NAME := libperipheral

#文件所在目录
SRC_DIR = $(CURDIR)
# 头文件所在目录
INC_DIR := $(SRC_DIR)/include

# 源文件和头文件
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:.c=.o)



CFLAGS := -g -I $(INC_DIR) $(LIBS) #-Wextra #-Wall 


# 默认生成可执行文件
.PHONY: cmd_svr
all: $(TARGET)
$(TARGET): $(filter-out $(SRC_DIR)/app_cmd_client.o,$(OBJS))
	$(CC) $(CFLAGS) -o $@ $^  


.PHONY: cmd_cli
cmd_cli: app_cmd_client.o
	$(CC) $(CFLAGS) -o $(TARGET2)  app_cmd_client.o

# 清除生成的文件
.PHONY: clean
clean:
	rm -f $(TARGET) $(TARGET2) $(STATIC_LIB_NAME).a $(OBJS)
