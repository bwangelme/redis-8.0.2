## Redis对象的引用计数机制

### 1. 引用计数的基本概念

Redis使用引用计数（Reference Counting）来管理对象的内存生命周期。每个Redis对象（`robj`）都有一个 `refcount` 字段来跟踪有多少地方引用了这个对象。

```1000:1005:src/server.h
#define OBJ_SHARED_REFCOUNT INT_MAX     /* Global object never destroyed. */
#define OBJ_STATIC_REFCOUNT (INT_MAX-1) /* Object allocated in the stack. */
#define OBJ_FIRST_SPECIAL_REFCOUNT OBJ_STATIC_REFCOUNT
struct redisObject {
    unsigned type:4;
    unsigned encoding:4;
    unsigned lru:LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
    int refcount;
    void *ptr;
};
```

### 2. 引用计数的三种状态

#### 2.1 普通引用计数（1, 2, 3, ...）
- 正常的引用计数，表示有多少地方引用了这个对象
- 当引用计数为0时，对象会被释放

#### 2.2 共享对象（`OBJ_SHARED_REFCOUNT = INT_MAX`）
- 全局共享对象，永远不会被销毁
- 主要用于小整数（0-9999）和常用的字符串常量
- 多个地方可以安全地引用同一个对象，无需担心内存管理

#### 2.3 静态对象（`OBJ_STATIC_REFCOUNT = INT_MAX-1`）
- 分配在栈上的对象，不应该被引用计数管理
- 如果尝试对静态对象进行引用计数操作，会触发panic

### 3. 引用计数的核心操作

#### 3.1 增加引用计数（`incrRefCount`）

```350:361:src/object.c
void incrRefCount(robj *o) {
    if (o->refcount < OBJ_FIRST_SPECIAL_REFCOUNT) {
        o->refcount++;
    } else {
        if (o->refcount == OBJ_SHARED_REFCOUNT) {
            /* Nothing to do: this refcount is immutable. */
        } else if (o->refcount == OBJ_STATIC_REFCOUNT) {
            serverPanic("You tried to retain an object allocated in the stack");
        }
    }
}
```

**逻辑说明：**
- 如果引用计数小于特殊值，正常递增
- 如果是共享对象，不做任何操作（引用计数不可变）
- 如果是静态对象，触发panic（不应该对栈对象进行引用计数）

#### 3.2 减少引用计数（`decrRefCount`）

```362:386:src/object.c
void decrRefCount(robj *o) {
    if (o->refcount == OBJ_SHARED_REFCOUNT)
        return; /* Nothing to do: this refcount is immutable. */

    if (unlikely(o->refcount <= 0)) {
        serverPanic("illegal decrRefCount for object with: type %u, encoding %u, refcount %d",
            o->type, o->encoding, o->refcount);
    }

    if (--(o->refcount) == 0) {
        switch(o->type) {
        case OBJ_STRING: freeStringObject(o); break;
        case OBJ_LIST: freeListObject(o); break;
        case OBJ_SET: freeSetObject(o); break;
        case OBJ_ZSET: freeZsetObject(o); break;
        case OBJ_HASH: freeHashObject(o); break;
        case OBJ_MODULE: freeModuleObject(o); break;
        case OBJ_STREAM: freeStreamObject(o); break;
        default: serverPanic("Unknown object type"); break;
        }
        zfree(o);
    }
}
```

**逻辑说明：**
- 如果是共享对象，直接返回（不进行任何操作）
- 检查引用计数是否合法（不能小于等于0）
- 减少引用计数，如果变为0，则释放对象

### 4. 对象回收机制

#### 4.1 自动回收
当对象的引用计数降为0时，Redis会自动调用相应的释放函数：

- `freeStringObject()` - 释放字符串对象
- `freeListObject()` - 释放列表对象  
- `freeSetObject()` - 释放集合对象
- `freeZsetObject()` - 释放有序集合对象
- `freeHashObject()` - 释放哈希对象
- `freeModuleObject()` - 释放模块对象
- `freeStreamObject()` - 释放流对象

#### 4.2 共享对象的优化

Redis预创建了10000个小整数对象作为共享对象：

```2095:2100:src/server.c
for (j = 0; j < OBJ_SHARED_INTEGERS; j++) {
    shared.integers[j] =
        makeObjectShared(createObject(OBJ_STRING,(void*)(long)j));
    initObjectLRUOrLFU(shared.integers[j]);
    shared.integers[j]->encoding = OBJ_ENCODING_INT;
}
```

这些共享对象：
- 永远不会被释放
- 多个地方可以安全引用
- 节省内存空间
- 提高性能（避免重复创建）

#### 4.3 内存策略对共享对象的影响

```159:169:src/object.c
robj *createStringObjectFromLongLongForValue(long long value) {
    if (server.maxmemory == 0 || !(server.maxmemory_policy & MAXMEMORY_FLAG_NO_SHARED_INTEGERS)) {
        /* If the maxmemory policy permits, we can still return shared integers */
        return createStringObjectFromLongLongWithOptions(value, LL2STROBJ_AUTO);
    } else {
        return createStringObjectFromLongLongWithOptions(value, LL2STROBJ_NO_SHARED);
    }
}
```

**重要发现：**
- 当Redis配置了内存限制且使用LRU/LFU策略时，不会使用共享整数
- 这是因为每个对象需要独立的LRU/LFU信息来进行内存淘汰决策
- 如果使用共享对象，所有引用该对象的key会共享相同的LRU/LFU信息，影响淘汰算法的准确性

### 5. 引用计数的使用场景

#### 5.1 命令执行中的引用计数
在命令执行过程中，Redis会：
- 增加输入参数的引用计数
- 减少临时对象的引用计数
- 确保返回给客户端的结果对象有正确的引用计数

#### 5.2 模块API中的引用计数
Redis模块API提供了引用计数管理：
- `RM_RetainString()` - 增加字符串引用计数
- `RM_HoldString()` - 持有字符串对象
- 自动内存管理确保模块不会泄漏内存

### 6. 总结

Redis的引用计数机制是一个高效的内存管理方案：

1. **自动内存管理**：当引用计数为0时自动释放对象
2. **共享对象优化**：通过共享常用对象节省内存
3. **线程安全**：共享对象可以在多线程间安全使用
4. **策略兼容**：根据内存淘汰策略动态调整共享对象的使用
5. **错误检测**：对非法操作进行panic保护

这种设计既保证了内存管理的正确性，又通过共享对象优化提高了性能和内存利用率。