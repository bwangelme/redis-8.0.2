## Redis 字典实现分析

### 1. 概述

Redis的字典（dict）是一个高性能的哈希表实现，是Redis数据库、过期键、Hash对象等多种数据结构的基础。它采用**链式哈希**解决冲突，支持**渐进式rehash**来实现动态扩容。

### 2. 核心数据结构

#### 2.1 字典节点 (dictEntry)

```c
struct dictEntry {
    void *key;          // 键指针
    union {
        void *val;      // 值指针
        uint64_t u64;   // 64位无符号整数
        int64_t s64;    // 64位有符号整数
        double d;       // 双精度浮点数
    } v;                // 值的联合体，支持多种类型
    struct dictEntry *next;  // 指向下一个节点的指针（链式哈希）
};
```

**特点：**
- 使用联合体存储值，优化内存使用
- `next`指针实现链式哈希，解决哈希冲突
- 支持直接存储基本数据类型，避免额外内存分配

#### 2.2 字典结构 (dict)

```c
struct dict {
    dictType *type;              // 字典类型，定义各种操作函数
    dictEntry **ht_table[2];     // 哈希表数组（双哈希表）
    unsigned long ht_used[2];    // 已使用的节点数量
    long rehashidx;              // rehash索引，-1表示没有进行rehash
    unsigned pauserehash : 15;   // 暂停rehash的计数器
    unsigned useStoredKeyApi : 1; // 使用存储键API标志
    signed char ht_size_exp[2];  // 哈希表大小的指数（size = 1<<exp）
    int16_t pauseAutoResize;     // 暂停自动resize的计数器
    void *metadata[];            // 元数据数组
};
```

**关键设计：**
- **双哈希表**：`ht_table[0]`和`ht_table[1]`，支持渐进式rehash
- **指数存储**：`ht_size_exp`存储指数，实际大小为`1<<exp`，保证是2的幂
- **rehash控制**：`rehashidx`追踪rehash进度，`pauserehash`控制暂停

### 3. 数据存储方式

#### 3.1 哈希表结构

```
Hash Table (ht_table[0])
┌─────────────────────────────────────────────────────────────┐
│  Index 0  │  Index 1  │  Index 2  │  ...  │  Index (size-1) │
├───────────┼───────────┼───────────┼───────┼─────────────────┤
│    NULL   │  Entry*   │    NULL   │  ...  │     Entry*      │
└───────────┴───────────┴───────────┴───────┴─────────────────┘
                 │                                    │
                 ▼                                    ▼
            ┌─────────┐                          ┌─────────┐
            │  Entry  │                          │  Entry  │
            │  key    │                          │  key    │
            │  value  │                          │  value  │
            │  next   │ ──────────────────────►  │  next   │
            └─────────┘                          └─────────┘
```

#### 3.2 哈希计算和索引

```c
// 哈希计算
uint64_t hash = dictHashKey(d, key, d->useStoredKeyApi);

// 索引计算（使用位运算优化）
idx = hash & DICTHT_SIZE_MASK(d->ht_size_exp[table]);
// DICTHT_SIZE_MASK(exp) = (1<<exp) - 1
```

**优势：**
- 使用位运算替代取模操作，提高性能
- 哈希表大小始终是2的幂，保证均匀分布

#### 3.3 冲突解决：链式哈希

```c
// 插入新节点到链表头部
entry->next = *bucket;
*bucket = entry;
```

**特点：**
- 新节点插入到链表头部，假设最近插入的数据被访问概率更高
- 查找时遍历链表，时间复杂度O(n)，n为链表长度

### 4. 渐进式Rehash机制

#### 4.1 双哈希表设计

Redis使用两个哈希表实现渐进式rehash：
- **ht_table[0]**：当前主要使用的哈希表
- **ht_table[1]**：rehash过程中的新哈希表

#### 4.2 Rehash触发条件

```c
// 扩容条件
if ((dict_can_resize == DICT_RESIZE_ENABLE &&
     d->ht_used[0] >= DICTHT_SIZE(d->ht_size_exp[0])) ||
    (dict_can_resize != DICT_RESIZE_FORBID &&
     d->ht_used[0] >= dict_force_resize_ratio * DICTHT_SIZE(d->ht_size_exp[0])))
```

**触发时机：**
- 负载因子 ≥ 1.0 且允许resize
- 负载因子 ≥ 4.0 且强制resize（即使在COW期间）

#### 4.3 Rehash过程

```c
static void rehashEntriesInBucketAtIndex(dict *d, uint64_t idx) {
    dictEntry *de = d->ht_table[0][idx];
    while (de) {
        dictEntry *nextde = dictGetNext(de);
        void *key = dictGetKey(de);
        
        // 计算新哈希表中的位置
        uint64_t h = dictHashKey(d, key, 1) & DICTHT_SIZE_MASK(d->ht_size_exp[1]);
        
        // 移动到新哈希表
        dictSetNext(de, d->ht_table[1][h]);
        d->ht_table[1][h] = de;
        
        d->ht_used[0]--;
        d->ht_used[1]++;
        de = nextde;
    }
    d->ht_table[0][idx] = NULL;
}
```

**过程：**
1. 创建新的更大的哈希表`ht_table[1]`
2. 设置`rehashidx = 0`，开始rehash过程
3. 每次操作时，迁移一个bucket的所有节点
4. 迁移完成后，释放旧表，新表成为主表

#### 4.4 渐进式特性

- **分步进行**：每次字典操作时执行一步rehash
- **时间分散**：避免长时间阻塞
- **双表查找**：rehash期间需要在两个表中查找

### 5. 内存优化技术

#### 5.1 指针位操作优化

```c
// 利用指针的最低3位进行标记
#define ENTRY_PTR_MASK        7 /* 111 */
#define ENTRY_PTR_NORMAL      0 /* 000 */
#define ENTRY_PTR_IS_ODD_KEY  1 /* XX1 */
#define ENTRY_PTR_IS_EVEN_KEY 2 /* 010 */
#define ENTRY_PTR_NO_VALUE    4 /* 100 */
```

**优化点：**
- 对于无值的字典（如Set），可以直接存储key而不分配dictEntry
- 利用指针对齐特性，使用最低位存储类型信息

#### 5.2 值的联合体存储

```c
union {
    void *val;      // 指针类型
    uint64_t u64;   // 整数类型
    int64_t s64;    // 有符号整数
    double d;       // 浮点数
} v;
```

**优势：**
- 避免为基本类型分配额外内存
- 减少内存碎片和指针解引用开销

### 6. 性能特点

#### 6.1 时间复杂度

- **插入/删除**：平均O(1)，最坏O(n)
- **查找**：平均O(1)，最坏O(n)
- **Rehash**：分摊O(1)

#### 6.2 空间复杂度

- **负载因子**：通常保持在0.5-1.0之间
- **内存开销**：每个节点额外开销为一个next指针
- **双表期间**：临时需要约2倍内存

### 7. 应用场景

Redis字典被广泛用于：
- **数据库键空间**：存储所有键值对
- **过期键字典**：存储键的过期时间
- **Hash对象**：当Hash对象较大时的底层实现
- **集合对象**：Set类型的底层实现
- **有序集合**：ZSet的成员到分值的映射

### 8. 设计优势

1. **高性能**：O(1)平均时间复杂度
2. **内存效率**：多种优化减少内存使用
3. **渐进式rehash**：避免长时间阻塞
4. **灵活性**：支持多种数据类型存储
5. **可扩展性**：动态调整大小适应数据量变化

### 9. 与其他实现的对比

与传统哈希表相比：
- **更好的实时性**：渐进式rehash避免阻塞
- **更低的内存碎片**：精心设计的内存布局
- **更强的灵活性**：支持多种存储优化

Redis的字典实现是一个经过高度优化的哈希表，在性能、内存效率和实时性方面都有出色的表现，是Redis高性能的重要基石。