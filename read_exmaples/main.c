#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 函数声明
void adlist_demo(void);

void print_usage(const char *program_name) {
    printf("用法: %s <命令>\n", program_name);
    printf("可用的命令:\n");
    printf("  adlist_demo  - 运行链表实现测试\n");
    printf("  help         - 显示此帮助信息\n");
    printf("\n示例:\n");
    printf("  %s adlist_demo\n", program_name);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("错误: 缺少参数\n\n");
        print_usage(argv[0]);
        return 1;
    }

    char *command = argv[1];

    if (strcmp(command, "adlist_demo") == 0) {
        printf("执行 adlist_demo 命令...\n\n");
        adlist_demo();
    } else if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
    } else {
        printf("错误: 未知命令 '%s'\n\n", command);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
} 