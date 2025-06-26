## Redis异步删除的bio.c队列工作机制

### 1. 整体架构设计

Redis的异步删除系统采用了**多线程工作队列**的设计模式：

```50:70:src/bio.c
static unsigned int bio_job_to_worker[] = {
    [BIO_CLOSE_FILE] = 0,
    [BIO_AOF_FSYNC] = 1,
    [BIO_CLOSE_AOF] = 1,
    [BIO_LAZY_FREE] = 2,
    [BIO_COMP_RQ_CLOSE_FILE] = 0,
    [BIO_COMP_RQ_AOF_FSYNC]  = 1,
    [BIO_COMP_RQ_LAZY_FREE]  = 2
};

static pthread_t bio_threads[BIO_WORKER_NUM];
static pthread_mutex_t bio_mutex[BIO_WORKER_NUM];
static pthread_cond_t bio_newjob_cond[BIO_WORKER_NUM];
static list *bio_jobs[BIO_WORKER_NUM];
static unsigned long bio_jobs_counter[BIO_NUM_OPS] = {0};
```

**核心组件：**
- **3个工作线程**：分别处理文件关闭、AOF同步、懒释放
- **3个队列**：每个工作线程对应一个任务队列
- **互斥锁和条件变量**：保证线程安全
- **任务计数器**：跟踪各类型任务的待处理数量

### 2. 任务提交机制

#### 2.1 任务提交函数

```181:190:src/bio.c
void bioSubmitJob(int type, bio_job *job) {
    job->header.type = type;
    unsigned long worker = bio_job_to_worker[type];
    pthread_mutex_lock(&bio_mutex[worker]);
    listAddNodeTail(bio_jobs[worker],job);
    bio_jobs_counter[type]++;
    pthread_cond_signal(&bio_newjob_cond[worker]);
    pthread_mutex_unlock(&bio_mutex[worker]);
}
```

**工作流程：**
1. 根据任务类型确定工作线程
2. 加锁保护队列
3. 将任务添加到队列尾部（FIFO）
4. 更新任务计数器
5. 发送条件变量信号唤醒工作线程
6. 释放锁

#### 2.2 懒释放任务创建

```191:205:src/bio.c
void bioCreateLazyFreeJob(lazy_free_fn free_fn, int arg_count, ...) {
    va_list valist;
    /* Allocate memory for the job structure and all required
     * arguments */
    bio_job *job = zmalloc(sizeof(*job) + sizeof(void *) * (arg_count));
    job->free_args.free_fn = free_fn;

    va_start(valist, arg_count);
    for (int i = 0; i < arg_count; i++) {
        job->free_args.free_args[i] = va_arg(valist, void *);
    }
    va_end(valist);
    bioSubmitJob(BIO_LAZY_FREE, job);
}
```

**特点：**
- 动态分配任务结构体
- 支持可变参数
- 自动提交到懒释放队列

### 3. 工作线程处理机制

#### 3.1 主循环逻辑

```254:320:src/bio.c
void *bioProcessBackgroundJobs(void *arg) {
    bio_job *job;
    unsigned long worker = (unsigned long) arg;
    sigset_t sigset;

    /* Check that the worker is within the right interval. */
    serverAssert(worker < BIO_WORKER_NUM);

    redis_set_thread_title(bio_worker_title[worker]);
    redisSetCpuAffinity(server.bio_cpulist);
    makeThreadKillable();

    pthread_mutex_lock(&bio_mutex[worker]);
    /* Block SIGALRM so we are sure that only the main thread will
     * receive the watchdog signal. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
        serverLog(LL_WARNING,
            "Warning: can't mask SIGALRM in bio.c thread: %s", strerror(errno));

    while(1) {
        listNode *ln;

        /* The loop always starts with the lock hold. */
        if (listLength(bio_jobs[worker]) == 0) {
            pthread_cond_wait(&bio_newjob_cond[worker], &bio_mutex[worker]);
            continue;
        }
        /* Get the job from the queue. */
        ln = listFirst(bio_jobs[worker]);
        job = ln->value;
        /* It is now possible to unlock the background system as we know have
         * a stand alone job structure to process.*/
        pthread_mutex_unlock(&bio_mutex[worker]);

        /* Process the job accordingly to its type. */
        int job_type = job->header.type;

        if (job_type == BIO_LAZY_FREE) {
            job->free_args.free_fn(job->free_args.free_args);
        }
        // ... 其他任务类型处理
        
        zfree(job);

        /* Lock again before reiterating the loop, if there are no longer
         * jobs to process we'll block again in pthread_cond_wait(). */
        pthread_mutex_lock(&bio_mutex[worker]);
        listDelNode(bio_jobs[worker], ln);
        bio_jobs_counter[job_type]--;
        pthread_cond_signal(&bio_newjob_cond[worker]);
    }
}
```

**关键步骤：**
1. **线程初始化**：设置线程名称、CPU亲和性、信号屏蔽
2. **等待任务**：使用条件变量等待新任务
3. **获取任务**：从队列头部取出任务（FIFO）
4. **释放锁**：处理任务时不需要持有锁
5. **执行任务**：根据任务类型调用相应处理函数
6. **清理任务**：释放任务内存，更新计数器

### 4. 异步删除的具体流程

#### 4.1 删除决策

```174:184:src/lazyfree.c
void freeObjAsync(robj *key, robj *obj, int dbid) {
    size_t free_effort = lazyfreeGetFreeEffort(key,obj,dbid);
    /* Note that if the object is shared, to reclaim it now it is not
     * possible. This rarely happens, however sometimes the implementation
     * of parts of the Redis core may call incrRefCount() to protect
     * objects, and then call dbDelete(). */
    if (free_effort > LAZYFREE_THRESHOLD && obj->refcount == 1) {
        atomicIncr(lazyfree_objects,1);
        bioCreateLazyFreeJob(lazyfreeFreeObject,1,obj);
    } else {
        decrRefCount(obj);
    }
}
```

**决策条件：**
- `free_effort > LAZYFREE_THRESHOLD`（64）：对象复杂度足够高
- `obj->refcount == 1`：对象没有被其他地方引用
- 满足条件则异步删除，否则同步删除

#### 4.2 复杂度评估

```113:172:src/lazyfree.c
size_t lazyfreeGetFreeEffort(robj *key, robj *obj, int dbid) {
    if (obj->type == OBJ_LIST && obj->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklist *ql = obj->ptr;
        return ql->len;
    } else if (obj->type == OBJ_SET && obj->encoding == OBJ_ENCODING_HT) {
        dict *ht = obj->ptr;
        return dictSize(ht);
    } else if (obj->type == OBJ_ZSET && obj->encoding == OBJ_ENCODING_SKIPLIST){
        zset *zs = obj->ptr;
        return zs->zsl->length;
    } else if (obj->type == OBJ_HASH && obj->encoding == OBJ_ENCODING_HT) {
        dict *ht = obj->ptr;
        return dictSize(ht);
    } else if (obj->type == OBJ_STREAM) {
        // 复杂的流对象计算
        size_t effort = 0;
        stream *s = obj->ptr;
        effort += s->rax->numnodes;
        // ... 更多计算
        return effort;
    } else {
        return 1; /* Everything else is a single allocation. */
    }
}
```

**复杂度计算规则：**
- **列表**：返回quicklist的长度
- **集合**：返回哈希表的大小
- **有序集合**：返回跳表的长度
- **哈希**：返回哈希表的大小
- **流**：返回rax树节点数
- **其他**：返回1（单个分配）

#### 4.3 实际释放函数

```13:18:src/lazyfree.c
void lazyfreeFreeObject(void *args[]) {
    robj *o = (robj *) args[0];
    decrRefCount(o);
    atomicDecr(lazyfree_objects,1);
    atomicIncr(lazyfreed_objects,1);
}
```

**释放过程：**
1. 调用`decrRefCount`释放对象
2. 原子递减待释放对象计数
3. 原子递增已释放对象计数

### 5. 完成通知机制

#### 5.1 完成请求创建

```206:228:src/bio.c
void bioCreateCompRq(bio_worker_t assigned_worker, comp_fn *func, uint64_t user_data, void *user_ptr) {
    bio_job *job = zmalloc(sizeof(*job));
    job->comp_rq.fn = func;
    job->comp_rq.arg = user_data;
    job->comp_rq.ptr = user_ptr;

    bioSubmitJob(BIO_COMP_RQ_LAZY_FREE, job);
}
```

#### 5.2 管道通知机制

```415:445:src/bio.c
void bioPipeReadJobCompList(aeEventLoop *el, int fd, void *privdata, int mask) {
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    char buf[128];
    list *tmp_list = NULL;

    while (read(fd, buf, sizeof(buf)) == sizeof(buf));

    /* Handle event loop events if pipe was written from event loop API */
    pthread_mutex_lock(&bio_mutex_comp);
    if (listLength(bio_comp_list)) {
        tmp_list = bio_comp_list;
        bio_comp_list = listCreate();
    }
    pthread_mutex_unlock(&bio_mutex_comp);

    if (!tmp_list) return;

    /* callback to all job completions  */
    while (listLength(tmp_list)) {
        listNode *ln = listFirst(tmp_list);
        bio_comp_item *rsp = ln->value;
        listDelNode(tmp_list, ln);
        rsp->func(rsp->arg, rsp->ptr);
        zfree(rsp);
    }
    listRelease(tmp_list);
}
```

**通知流程：**
1. 工作线程完成任务后写入管道
2. 主线程通过事件循环读取管道
3. 处理完成回调列表
4. 执行用户回调函数

### 6. 队列监控和管理

#### 6.1 任务计数查询

```371:380:src/bio.c
unsigned long bioPendingJobsOfType(int type) {
    unsigned int worker = bio_job_to_worker[type];

    pthread_mutex_lock(&bio_mutex[worker]);
    unsigned long val = bio_jobs_counter[type];
    pthread_mutex_unlock(&bio_mutex[worker]);

    return val;
}
```

#### 6.2 队列清空等待

```382:395:src/bio.c
void bioDrainWorker(int job_type) {
    unsigned long worker = bio_job_to_worker[job_type];

    pthread_mutex_lock(&bio_mutex[worker]);
    while (listLength(bio_jobs[worker]) > 0) {
        pthread_cond_wait(&bio_newjob_cond[worker], &bio_mutex[worker]);
    }
    pthread_mutex_unlock(&bio_mutex[worker]);
}
```

### 7. 总结

Redis的bio.c队列系统具有以下特点：

1. **多线程设计**：3个专门的工作线程处理不同类型的后台任务
2. **FIFO队列**：保证任务按提交顺序处理
3. **线程安全**：使用互斥锁和条件变量保证并发安全
4. **智能决策**：根据对象复杂度决定是否异步删除
5. **完成通知**：通过管道机制通知主线程任务完成
6. **监控统计**：提供任务计数和队列状态查询
7. **优雅关闭**：支持队列清空和线程终止

这种设计既保证了Redis主线程的响应性，又通过异步处理提高了内存释放的效率，特别是在处理大型对象时效果显著。