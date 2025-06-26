## Redis 链表实现分析

### 1. 文件结构概览

Redis 的链表实现包含两个文件：
- **`adlist.h`**：头文件，定义数据结构和函数声明
- **`adlist.c`**：实现文件，包含所有函数的具体实现

### 2. 数据结构定义

#### 2.1 核心数据结构

```c
// 链表节点
typedef struct listNode {
    struct listNode *prev;  // 前驱节点指针
    struct listNode *next;  // 后继节点指针
    void *value;           // 节点值（泛型指针）
} listNode;

// 链表迭代器
typedef struct listIter {
    listNode *next;        // 下一个要访问的节点
    int direction;         // 迭代方向（0:从头开始，1:从尾开始）
} listIter;

// 链表结构
typedef struct list {
    listNode *head;        // 头节点指针
    listNode *tail;        // 尾节点指针
    void *(*dup)(void *ptr);    // 复制函数指针
    void (*free)(void *ptr);    // 释放函数指针
    int (*match)(void *ptr, void *key);  // 匹配函数指针
    unsigned long len;     // 链表长度
} list;
```

### 3. 函数功能分析

#### 3.1 基础操作函数

**创建和销毁：**
- `listCreate()` - 创建新的空链表
- `listRelease()` - 释放整个链表及其所有节点
- `listEmpty()` - 清空链表内容但保留链表结构

**节点操作：**
- `listAddNodeHead()` - 在链表头部添加新节点
- `listAddNodeTail()` - 在链表尾部添加新节点
- `listInsertNode()` - 在指定节点前后插入新节点
- `listDelNode()` - 删除指定节点并释放内存
- `listUnlinkNode()` - 从链表中移除节点但不释放内存

#### 3.2 迭代器相关函数

- `listGetIterator()` - 创建链表迭代器
- `listNext()` - 获取迭代器的下一个节点
- `listReleaseIterator()` - 释放迭代器
- `listRewind()` - 重置迭代器到链表头部
- `listRewindTail()` - 重置迭代器到链表尾部

#### 3.3 查找和访问函数

- `listSearchKey()` - 根据键值查找节点
- `listIndex()` - 根据索引获取节点（支持负数索引）
- `listDup()` - 复制整个链表

#### 3.4 高级操作函数

- `listRotateTailToHead()` - 将尾节点移到头部（轮转）
- `listRotateHeadToTail()` - 将头节点移到尾部（轮转）
- `listJoin()` - 将两个链表合并
- `listInitNode()` - 初始化节点
- `listLinkNodeHead()` - 将已分配的节点链接到头部
- `listLinkNodeTail()` - 将已分配的节点链接到尾部

### 4. 设计特点

#### 4.1 泛型设计
- 使用 `void *` 指针存储节点值，支持任意数据类型
- 通过函数指针实现自定义的复制、释放和匹配操作

#### 4.2 双向链表
- 每个节点都有前驱和后继指针，支持双向遍历
- 维护头尾指针，支持 O(1) 的头部和尾部操作

#### 4.3 内存管理
- 使用 Redis 自定义的内存分配器 `zmalloc` 和 `zfree`
- 支持自定义释放函数，确保正确释放节点值

#### 4.4 迭代器模式
- 提供安全的遍历机制
- 支持正向和反向遍历
- 在遍历过程中可以安全删除当前节点

### 5. 使用示例

详见 ./adlist_demo


### 6. 性能特点

- **插入/删除**：头部和尾部操作 O(1)，中间插入 O(1)
- **查找**：O(n)，需要遍历链表
- **索引访问**：O(n)，需要从头或尾遍历到指定位置
- **空间复杂度**：每个节点额外开销为两个指针（prev, next）

这个链表实现是 Redis 中很多其他数据结构的基础，比如列表类型的底层实现就使用了这个双向链表。它的设计非常通用和高效，是 Redis 源码中值得学习的经典实现。