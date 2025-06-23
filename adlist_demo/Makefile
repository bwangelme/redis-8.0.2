# 定义编译器和编译选项
CC = gcc
CFLAGS = -Wall -g
LDFLAGS =

# 定义目标文件和依赖项
TARGET = exec_bin_adlist_demo
SRCS = ../src/adlist.c zmalloc.c adlist_demo.c
OBJS = $(SRCS:.c=.o)

# 默认目标：编译程序
all: $(TARGET)

# 链接目标文件生成可执行程序
$(TARGET): $(OBJS)
	@echo "编译 Redis 链表测试程序..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "编译成功！"

# 编译每个 .c 文件为 .o 文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 运行测试程序
run: $(TARGET)
	@echo "运行测试程序："
	./$(TARGET)

# 清理生成的文件
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "已清理生成的文件."

.PHONY: all run clean