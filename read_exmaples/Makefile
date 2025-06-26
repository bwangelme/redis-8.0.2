# 定义编译器和编译选项
CC = gcc
CFLAGS = -Wall -g
LDFLAGS =

# 定义目标文件和依赖项
TARGET_MAIN = main
SRCS_MAIN = ../src/adlist.c zmalloc.c adlist_demo.c main.c
OBJS_MAIN = $(SRCS_MAIN:.c=.o)

# 默认目标：编译主程序
all: $(TARGET_MAIN)

# 编译主程序
$(TARGET_MAIN): $(OBJS_MAIN)
	@echo "编译主程序..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "主程序编译成功！"

# 编译每个 .c 文件为 .o 文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 运行主程序
run: $(TARGET_MAIN)
	@echo "运行主程序："
	@echo "用法: ./$(TARGET_MAIN) <命令>"
	@echo "可用命令: adlist_demo, help"

# 运行帮助
help: $(TARGET_MAIN)
	@echo "显示帮助信息："
	./$(TARGET_MAIN) help

# 清理生成的文件
clean:
	rm -f $(OBJS_MAIN) $(TARGET_MAIN)
	@echo "已清理生成的文件."

.PHONY: all run help clean