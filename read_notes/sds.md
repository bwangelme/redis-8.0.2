## 内存布局

首先，让我们看看 SDS 的内存布局：

```
+----------------+----------------+----------------+
|     header     |    content    |    '\0'       |
+----------------+----------------+----------------+
^                ^                ^
sh               s               s+len
```

具体分析：

1. **内存布局**：
   - `sh`：指向整个 SDS 结构体的起始位置（包括头部）
   - `s`：指向实际字符串内容的起始位置
   - `s[-1]`：通过指针运算，访问 flags 字段

2. **指针运算**：
```c
// 假设我们有一个 SDS 字符串 s
sds s = sdsnew("Hello");

// s 指向字符串内容的起始位置
// s[-1] 相当于 *(s-1)，即访问 s 前一个字节
unsigned char flags = s[-1];
```

3. **为什么这样设计**：
   - SDS 字符串的头部和内容在内存中是连续的
   - 头部在内容之前
   - 通过指针运算可以快速访问头部信息

让我们看一个具体的例子：

```c
// 假设我们有一个 SDS_TYPE_8 类型的字符串
struct sdshdr8 {
    uint8_t len;        // 1 字节
    uint8_t alloc;      // 1 字节
    unsigned char flags; // 1 字节
    char buf[];         // 柔性数组
};

// 内存布局：
// +--------+--------+--------+----------------+
// |  len   | alloc  | flags  |    content    |
// +--------+--------+--------+----------------+
// ^        ^        ^        ^
// sh       sh+1     sh+2     s
```

当我们需要访问 flags 时：
```c
// s 指向 content 的起始位置
// s[-1] 就是 flags 的位置
unsigned char flags = s[-1];

// 这等价于：
unsigned char flags = *(s-1);
```

这种设计的优点：

1. **快速访问**：
   - 不需要额外的指针来访问头部
   - 通过简单的指针运算就能获取类型信息

2. **内存效率**：
   - 头部和内容紧密相连
   - 没有额外的指针开销

3. **类型安全**：
   - 通过 flags 字段可以确定具体的头部类型
   - 可以正确访问对应的头部结构

4. **二进制安全**：
   - 不依赖 '\0' 作为结束符
   - 通过 len 字段记录实际长度

使用示例：
```c
sds s = sdsnew("Hello");
unsigned char flags = s[-1];  // 获取类型标志
char type = flags & SDS_TYPE_MASK;  // 获取具体类型

// 根据类型访问对应的头部
switch(type) {
    case SDS_TYPE_8:
        return SDS_HDR(8,s)->len;
    case SDS_TYPE_16:
        return SDS_HDR(16,s)->len;
    // ...
}
```

这种设计是 SDS 实现中的一个关键点，它使得：
1. 字符串操作更加高效
2. 内存使用更加紧凑
3. 类型信息可以快速获取
4. 头部访问更加灵活

这就是为什么 `s[-1]` 能够访问到 flags 字段的原因，它是 SDS 内存布局设计的核心部分。

## sds 操作函数

好的，让我们详细分析 SDS 字符串操作的具体实现。我会从几个主要的操作函数来分析：

1. **字符串创建**：
```c
sds sdsnewlen(const void *init, size_t initlen) {
    return _sdsnewlen(init, initlen, 0);
}

sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}
```
- 根据初始内容创建 SDS 字符串
- 自动选择合适的头部类型
- 分配足够的内存空间

2. **字符串追加**：
```c
sds sdscatlen(sds s, const void *t, size_t len) {
    size_t curlen = sdslen(s);
    s = sdsMakeRoomFor(s,len);  // 确保有足够空间
    if (s == NULL) return NULL;
    memcpy(s+curlen, t, len);   // 复制新内容
    sdssetlen(s, curlen+len);   // 更新长度
    s[curlen+len] = '\0';       // 添加结束符
    return s;
}
```
- 先检查并扩展空间
- 追加新内容
- 更新长度信息

3. **字符串复制**：
```c
sds sdscpylen(sds s, const char *t, size_t len) {
    if (sdsalloc(s) < len) {
        s = sdsMakeRoomFor(s,len-sdslen(s));
        if (s == NULL) return NULL;
    }
    memcpy(s, t, len);
    s[len] = '\0';
    sdssetlen(s, len);
    return s;
}
```
- 确保目标有足够空间
- 复制内容
- 更新长度

4. **字符串格式化**：
```c
sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *t;
    va_start(ap, fmt);
    t = sdscatvprintf(s,fmt,ap);
    va_end(ap);
    return t;
}
```
- 支持类似 printf 的格式化
- 自动处理内存分配
- 支持多种格式化选项

5. **字符串清理**：
```c
void sdsclear(sds s) {
    sdssetlen(s, 0);
    s[0] = '\0';
}
```
- 清空字符串内容
- 保留已分配的空间
- 设置长度为 0

6. **字符串比较**：
```c
int sdscmp(const sds s1, const sds s2) {
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1,s2,minlen);
    if (cmp == 0) return l1-l2;
    return cmp;
}
```
- 比较两个 SDS 字符串
- 先比较共同长度部分
- 再比较长度差异

7. **字符串分割**：
```c
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count)
```
- 按分隔符分割字符串
- 返回分割后的字符串数组
- 支持二进制安全

8. **字符串转换**：
```c
void sdstolower(sds s)  // 转小写
void sdstoupper(sds s)  // 转大写
```
- 原地修改字符串
- 不重新分配内存
- 直接修改字符

9. **字符串缩容**

```c
sds sdsRemoveFreeSpace(sds s, int would_regrow)
```

- 删除字符串后面的空白字符
- 如果新类型比原来类型小，使用原来的空间，调用 malloc 进行 realloc

## SDS 字符串操作的特点

1. **二进制安全**：
   - 可以存储任意二进制数据
   - 不依赖 '\0' 作为结束符
   - 使用长度字段记录实际长度

2. **内存优化**：
   - 预分配空间减少重分配
   - 使用不同的头部类型
   - 自动管理内存

3. **性能优化**：
   - 快速的长度获取
   - 高效的追加操作
   - 最小化内存分配

4. **安全性**：
   - 检查内存分配失败
   - 防止缓冲区溢出
   - 处理空指针情况
