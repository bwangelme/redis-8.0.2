#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/adlist.h"

// 字符串匹配函数
int stringMatch(void *ptr, void *key) {
    return strcmp((char*)ptr, (char*)key) == 0;
}

// 字符串复制函数
void *stringDup(void *ptr) {
    return strdup((char*)ptr);
}

// 字符串释放函数
void stringFree(void *ptr) {
    free(ptr);
}

int main() {
    printf("=== Redis 链表实现测试 ===\n\n");
    
    // 创建链表
    list *myList = listCreate();
    if (!myList) {
        printf("创建链表失败！\n");
        return 1;
    }
    
    // 设置自定义函数
    listSetDupMethod(myList, stringDup);
    listSetFreeMethod(myList, stringFree);
    listSetMatchMethod(myList, stringMatch);
    
    printf("1. 添加节点到链表：\n");
    
    // 添加节点 - 使用动态分配的字符串
    listAddNodeHead(myList, strdup("hello"));
    listAddNodeTail(myList, strdup("world"));
    listAddNodeTail(myList, strdup("redis"));
    listAddNodeTail(myList, strdup("linked"));
    listAddNodeTail(myList, strdup("list"));
    
    printf("   添加了 5 个节点\n");
    printf("   链表长度: %lu\n\n", listLength(myList));
    
    // 正向遍历
    printf("2. 正向遍历链表：\n");
    listIter *iter = listGetIterator(myList, AL_START_HEAD);
    listNode *node;
    int count = 0;
    while ((node = listNext(iter)) != NULL) {
        printf("   节点 %d: %s\n", count++, (char*)node->value);
    }
    listReleaseIterator(iter);
    printf("\n");
    
    // 反向遍历
    printf("3. 反向遍历链表：\n");
    iter = listGetIterator(myList, AL_START_TAIL);
    count = 0;
    while ((node = listNext(iter)) != NULL) {
        printf("   节点 %d: %s\n", count++, (char*)node->value);
    }
    listReleaseIterator(iter);
    printf("\n");
    
    // 查找节点
    printf("4. 查找节点：\n");
    listNode *found = listSearchKey(myList, "redis");
    if (found) {
        printf("   找到节点: %s\n", (char*)found->value);
    } else {
        printf("   未找到节点 'redis'\n");
    }
    
    found = listSearchKey(myList, "notfound");
    if (found) {
        printf("   找到节点: %s\n", (char*)found->value);
    } else {
        printf("   未找到节点 'notfound'\n");
    }
    printf("\n");
    
    // 索引访问
    printf("5. 索引访问：\n");
    listNode *indexNode = listIndex(myList, 0);
    if (indexNode) {
        printf("   索引 0: %s\n", (char*)indexNode->value);
    }
    
    indexNode = listIndex(myList, 2);
    if (indexNode) {
        printf("   索引 2: %s\n", (char*)indexNode->value);
    }
    
    indexNode = listIndex(myList, -1);
    if (indexNode) {
        printf("   索引 -1: %s\n", (char*)indexNode->value);
    }
    printf("\n");
    
    // 删除节点
    printf("6. 删除节点：\n");
    printf("   删除前的链表长度: %lu\n", listLength(myList));
    
    // 找到并删除 "redis" 节点
    found = listSearchKey(myList, "redis");
    if (found) {
        listDelNode(myList, found);
        printf("   删除了节点 'redis'\n");
    }
    
    printf("   删除后的链表长度: %lu\n", listLength(myList));
    
    // 再次遍历查看结果
    printf("   删除后的链表内容：\n");
    iter = listGetIterator(myList, AL_START_HEAD);
    count = 0;
    while ((node = listNext(iter)) != NULL) {
        printf("     节点 %d: %s\n", count++, (char*)node->value);
    }
    listReleaseIterator(iter);
    printf("\n");
    
    // 测试链表复制
    printf("7. 测试链表复制：\n");
    list *copyList = listDup(myList);
    if (copyList) {
        printf("   复制成功，复制链表长度: %lu\n", listLength(copyList));
        
        printf("   复制链表内容：\n");
        iter = listGetIterator(copyList, AL_START_HEAD);
        count = 0;
        while ((node = listNext(iter)) != NULL) {
            printf("     节点 %d: %s\n", count++, (char*)node->value);
        }
        listReleaseIterator(iter);
        
        listRelease(copyList);
        printf("   复制链表已释放\n");
    } else {
        printf("   复制失败\n");
    }
    printf("\n");
    
    // 释放链表
    printf("8. 释放链表\n");
    listRelease(myList);
    printf("   链表已释放\n");
    
    printf("\n=== 测试完成 ===\n");
    return 0;
} 