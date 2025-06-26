## Redis删除过期key的代码逻辑

Redis删除过期key主要通过两种方式：**惰性删除（Lazy Expiration）**和**主动删除（Active Expiration）**。

### 1. 惰性删除（Lazy Expiration）

**核心函数：`expireIfNeeded`** (在 `src/db.c` 中)

- 2271:2314:src/db.c
```c
static inline keyStatus expireIfNeededWithSlot(redisDb *db, robj *key, int flags, const int keySlot) {
    if ((server.allow_access_expired) ||
        (flags & EXPIRE_ALLOW_ACCESS_EXPIRED) ||
        (!keyIsExpiredWithSlot(db,key,keySlot)))
        return KEY_VALID;

    /* If we are running in the context of a replica, instead of
     * evicting the expired key from the database, we return ASAP:
     * the replica key expiration is controlled by the master that will
     * send us synthesized DEL operations for expired keys. The
     * exception is when write operations are performed on writable
     * replicas.
     *
     * Still we try to return the right information to the caller,
     * that is, KEY_VALID if we think the key should still be valid,
     * KEY_EXPIRED if we think the key is expired but don't want to delete it at this time.
     *
     * When replicating commands from the master, keys are never considered
     * expired. */
    if (server.masterhost != NULL) {
        if (server.current_client && (server.current_client->flags & CLIENT_MASTER)) return KEY_VALID;
        if (!(flags & EXPIRE_FORCE_DELETE_EXPIRED)) return KEY_EXPIRED;
    }

    /* In some cases we're explicitly instructed to return an indication of a
     * missing key without actually deleting it, even on masters. */
    if (flags & EXPIRE_AVOID_DELETE_EXPIRED)
        return KEY_EXPIRED;

    /* If 'expire' action is paused, for whatever reason, then don't expire any key.
     * Typically, at the end of the pause we will properly expire the key OR we
     * will have failed over and the new primary will send us the expire. */
    if (isPausedActionsWithUpdate(PAUSE_ACTION_EXPIRE)) return KEY_EXPIRED;

    /* Delete the key */
    deleteExpiredKeyAndPropagate(db,key);

    return KEY_DELETED;
}
```

**触发时机：**
- 当客户端访问某个key时，通过 `lookupKey` 函数调用
- 在 `src/db.c:163` 中可以看到调用逻辑

**工作流程：**
1. 检查key是否过期
2. 如果是主节点且key已过期，则删除该key
3. 如果是从节点，通常不删除，等待主节点发送DEL命令

### 2. 主动删除（Active Expiration）

**核心函数：`activeExpireCycle`** (在 `src/expire.c` 中)

- 37:55:src/expire.c

```c
int activeExpireCycleTryExpire(redisDb *db, dictEntry *de, long long now) {
    long long t = dictGetSignedIntegerVal(de);
    if (now < t)
        return 0;

    enterExecutionUnit(1, 0);
    sds key = dictGetKey(de);
    robj *keyobj = createStringObject(key,sdslen(key));
    deleteExpiredKeyAndPropagate(db,keyobj);
    decrRefCount(keyobj);
    exitExecutionUnit();
    /* Propagate the DEL command */
    postExecutionUnitOperations();
    return 1;
}
```

**触发时机：**
- 慢速周期：每 `server.hz` 次（通常10次/秒）
- 快速周期：每次事件循环的 `beforeSleep()` 函数中

**工作流程：**
1. 扫描过期字典（`db->expires`）
2. 随机选择一些key检查是否过期
3. 删除已过期的key
4. 根据配置的"expire effort"调整工作强度

### 3. 实际删除操作

**核心删除函数：`deleteExpiredKeyAndPropagate`**

```2169:2172:src/db.c
void deleteExpiredKeyAndPropagate(redisDb *db, robj *keyobj) {
    deleteKeyAndPropagate(db, keyobj, NOTIFY_EXPIRED, NULL);
}
```

**具体删除实现：`dbGenericDelete`**

- 471:510:src/db.c

```c
int dbGenericDelete(redisDb *db, robj *key, int async, int flags) {
    dictEntry **plink;
    int table;
    int slot = getKeySlot(key->ptr);
    dictEntry *de = kvstoreDictTwoPhaseUnlinkFind(db->keys, slot, key->ptr, &plink, &table);
    if (de) {
        robj *val = dictGetVal(de);

        /* remove key from histogram */
        updateKeysizesHist(db, slot, val->type, getObjectLength(val), -1);

        /* If hash object with expiry on fields, remove it from HFE DS of DB */
        if (val->type == OBJ_HASH)
            hashTypeRemoveFromExpires(&db->hexpires, val);

        /* RM_StringDMA may call dbUnshareStringValue which may free val, so we
         * need to incr to retain val */
        incrRefCount(val);
        /* Tells the module that the key has been unlinked from the database. */
        moduleNotifyKeyUnlink(key,val,db->id,flags);
        /* We want to try to unblock any module clients or clients using a blocking XREADGROUP */
        signalDeletedKeyAsReady(db,key,val->type);
        /* We should call decr before freeObjAsync. If not, the refcount may be
         * greater than 1, so freeObjAsync doesn't work */
        decrRefCount(val);
        if (async) {
            /* Because of dbUnshareStringValue, the val in de may change. */
            freeObjAsync(key, dictGetVal(de), db->id);
            kvstoreDictSetVal(db->keys, slot, de, NULL);
        }
        /* Deleting an entry from the expires dict will not free the sds of
         * the key, because it is shared with the main dictionary. */
        kvstoreDictDelete(db->expires, slot, key->ptr);

        kvstoreDictTwoPhaseUnlinkFree(db->keys, slot, de, plink, table);
        return 1;
    } else {
        return 0;
    }
}
```

### 4. 关键数据结构

- **`db->keys`**: 存储所有key-value的主字典
- **`db->expires`**: 存储过期时间的字典
- **`db->hexpires`**: 存储hash字段过期的数据结构

### 5. 删除流程总结

1. **检查过期**: `keyIsExpiredWithSlot()` 检查key是否过期
2. **决定删除**: `expireIfNeeded()` 决定是否删除
3. **执行删除**: `deleteExpiredKeyAndPropagate()` 执行删除
4. **清理内存**: `dbGenericDelete()` 从字典中移除key和value
5. **传播删除**: 向从节点和AOF传播DEL命令
6. **通知事件**: 触发keyspace事件通知

这种设计确保了Redis能够高效地管理内存，同时保持数据的一致性。

## `activeExpireCycle` 函数详细分析

### 1. 函数概述

`activeExpireCycle` 是Redis中主动过期删除的核心函数，它通过两种模式运行：
- **快速模式**（`ACTIVE_EXPIRE_CYCLE_FAST`）：在每次事件循环中调用，处理时间短
- **慢速模式**（`ACTIVE_EXPIRE_CYCLE_SLOW`）：在主定时器中调用，处理时间较长

### 2. 关键参数和配置

- 187:200:src/expire.c

```c
void activeExpireCycle(int type) {
    /* Adjust the running parameters according to the configured expire
     * effort. The default effort is 1, and the maximum configurable effort
     * is 10. */
    unsigned long
    effort = server.active_expire_effort-1, /* Rescale from 0 to 9. */
    config_keys_per_loop = ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP +
                           ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP/4*effort,
    config_cycle_fast_duration = ACTIVE_EXPIRE_CYCLE_FAST_DURATION +
                                 ACTIVE_EXPIRE_CYCLE_FAST_DURATION/4*effort,
    config_cycle_slow_time_perc = ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC +
                                  2*effort,
    config_cycle_acceptable_stale = ACTIVE_EXPIRE_CYCLE_ACCEPTABLE_STALE-
                                    effort;
```

**关键常量：**
- `ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP = 20`：每次循环检查的key数量
- `ACTIVE_EXPIRE_CYCLE_FAST_DURATION = 1000`：快速模式最大执行时间（微秒）
- `ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC = 25`：慢速模式最大CPU使用百分比
- `ACTIVE_EXPIRE_CYCLE_ACCEPTABLE_STALE = 10`：可接受的过期key百分比

### 3. 过期检查的核心逻辑

#### 3.1 单个key的过期检查

- 38:52:src/expire.c

```c
int activeExpireCycleTryExpire(redisDb *db, dictEntry *de, long long now) {
    long long t = dictGetSignedIntegerVal(de);
    if (now < t)
        return 0;

    enterExecutionUnit(1, 0);
    sds key = dictGetKey(de);
    robj *keyobj = createStringObject(key,sdslen(key));
    deleteExpiredKeyAndPropagate(db,keyobj);
    decrRefCount(keyobj);
    exitExecutionUnit();
    /* Propagate the DEL command */
    postExecutionUnitOperations();
    return 1;
}
```

**关键步骤：**
1. 获取key的过期时间
2. 比较当前时间与过期时间
3. 如果已过期，调用 `deleteExpiredKeyAndPropagate` 删除key
4. 执行传播操作（AOF和复制）

#### 4.3 懒删除的配置

Redis中懒删除主要通过以下配置控制：
- `lazyfree-lazy-expire`：过期key是否使用懒删除
- `lazyfree-lazy-eviction`：内存淘汰时是否使用懒删除
- `lazyfree-lazy-server-del`：服务器删除时是否使用懒删除