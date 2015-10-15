#include <assert.h>
#include <errno.h>
#include <string.h>
#include <libmemcached/memcached.h>

#include "lua.h"
#include "lauxlib.h"

#define MC_STATE        "memcached state"

#define FLAG_NONE       0
#define FLAG_BOOLEAN    1
#define FLAG_NUMBER     2
#define FLAG_ENCODED    7

#if LUA_VERSION_NUM == 501
#define l_setfuncs(L, funcs)    luaL_register(L, NULL, funcs)
#define l_objlen                lua_objlen
#else
#define l_setfuncs(L, funcs)    luaL_setfuncs(L, funcs, 0)
#define l_objlen                lua_rawlen
#endif

#define DECODE_VALUE                                        \
    switch(flags) {                                         \
        case FLAG_ENCODED:                                  \
            lua_rawgeti(L, LUA_REGISTRYINDEX, d->decode);   \
            lua_pushlstring(L, value, value_length);        \
            free(value);                                    \
            lua_call(L, 1, 1);                              \
            break;                                          \
                                                            \
        case FLAG_NUMBER:                                   \
            lua_pushlstring(L, value, value_length);        \
            free(value);                                    \
            lua_Number number = lua_tonumber(L, -1);        \
            lua_pop(L, 1);                                  \
            lua_pushnumber(L, number);                      \
            break;                                          \
                                                            \
        case FLAG_BOOLEAN:                                  \
            lua_pushboolean(L, *value == '1' ? 1 : 0);      \
            free(value);                                    \
            break;                                          \
                                                            \
        default:                                            \
            lua_pushlstring(L, value, value_length);        \
            free(value);                                    \
            break;                                          \
    }                                                       \

#define BEHAVIOR(NAME)                                      \
    lua_pushinteger(L, MEMCACHED_BEHAVIOR_ ## NAME);        \
    lua_setfield(L, -2, #NAME);                             \


typedef memcached_return_t
(memcached_set_pt)(memcached_st *ptr, const char *key, size_t key_length,
                   const char *value, size_t value_length, time_t expiration,
                   uint32_t flags);

typedef memcached_return_t
(memcached_incr_pt)(memcached_st *ptr, const char *key, size_t key_length,
                    uint32_t offset, uint64_t *value);


typedef struct {
    memcached_st    *mc;
    int             key_encode;
    int             encode;
    int             decode;
} mc_data;


static int
l_new(lua_State *L)
{
    int key_encode, encode, decode;
    const char *config_string = luaL_checkstring(L, 1);

    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "encode");
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "bad argument #2 ('encode' "
                             "function is missing)");
    }

    lua_getfield(L, 2, "decode");
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "bad argument #2 ('decode' "
                             "function is missing)");
    }

    if (!lua_isnone(L, 3) && !lua_isfunction(L, 3)) {
        return luaL_error(L, "bad argument #3 ('key_encode' "
                             "must be a function)");
    }

    memcached_st *mc = memcached(config_string, strlen(config_string));
    if (!mc) {
        return luaL_error(L, "cannot allocate memcached object");
    }

    decode = luaL_ref(L, LUA_REGISTRYINDEX);
    encode = luaL_ref(L, LUA_REGISTRYINDEX);
    key_encode = !lua_isnone(L, 3) ? luaL_ref(L, LUA_REGISTRYINDEX) : 0;

    mc_data *d = (mc_data *)lua_newuserdata(L, sizeof(mc_data));
    luaL_getmetatable(L, MC_STATE);
    lua_setmetatable(L, -2);

    d->mc = mc;
    d->key_encode = key_encode;
    d->encode = encode;
    d->decode = decode;

    return 1;
}


static int
l_free(lua_State *L, mc_data *d)
{
    memcached_free(d->mc);
    d->mc = NULL;

    if (d->key_encode) {
        luaL_unref(L, LUA_REGISTRYINDEX, d->key_encode);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, d->encode);
    luaL_unref(L, LUA_REGISTRYINDEX, d->decode);

    return 0;
}


static int
l_gc(lua_State *L)
{
    mc_data *d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);

    if (d->mc != NULL) {
        l_free(L, d);
    }

    return 0;
}


static int
l_close(lua_State *L)
{
    mc_data *d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);

    if (d->mc == NULL) {
        lua_pushboolean (L, 0);
        return 1;
    }

    l_free(L, d);

    lua_pushboolean (L, 1);
    return 1;
}


static int
l_error(lua_State *L, const memcached_return_t rc)
{
    lua_pushnil(L);

    if (rc == MEMCACHED_ERRNO) {
        lua_pushstring(L, strerror(errno));
    }
    else {
        const mc_data *d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
        lua_pushstring(L, memcached_last_error_message(d->mc));
    }

    return 2;
}


static inline const char *
l_key_encode(lua_State *L, const mc_data *d, int narg, size_t *key_length)
{
    const char *key = luaL_checklstring(L, narg, key_length);
    if (*key_length >= MEMCACHED_MAX_KEY) {
        if (d->key_encode) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, d->key_encode);
            lua_pushvalue(L, narg);
            lua_call(L, 1, 1);
            key = luaL_checklstring(L, -1, key_length);
            lua_pop(L, 1);
        }
        else {
            luaL_error(L, "key is too long");
            return 0;
        }
    }

    return key;
}


static int
l_get_behavior(lua_State *L)
{
    const mc_data *d;
    memcached_behavior_t flag;
    uint64_t r;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    flag = luaL_checknumber(L, 2);

    r = memcached_behavior_get(d->mc, flag);

    lua_pushnumber(L, r);

    return 1;
}


static int
l_set_behavior(lua_State *L)
{
    const mc_data *d;
    memcached_behavior_t flag;
    uint64_t data;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    flag = luaL_checknumber(L, 2);
    data = luaL_checknumber(L, 3);

    rc = memcached_behavior_set(d->mc, flag, data);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }

    return l_error(L, rc);
}


#if LIBMEMCACHED_VERSION_HEX >= 0x01000006

static int
l_set_encoding_key(lua_State *L)
{
    const mc_data *d;
    const char *key;
    size_t key_length;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = luaL_checklstring(L, 2, &key_length);

    rc = memcached_set_encoding_key(d->mc, key, key_length);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }

    return l_error(L, rc);
}

#endif


static int
l_get(lua_State *L)
{
    const mc_data *d;
    const char *key;
    size_t key_length;
    char *value;
    size_t value_length;
    uint32_t flags;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = l_key_encode(L, d, 2, &key_length);

    value = memcached_get(d->mc, key, key_length, &value_length, &flags, &rc);

    if (value != NULL) {
        DECODE_VALUE
        return 1;
    }
    else if (rc == MEMCACHED_SUCCESS) {
        lua_pushliteral(L, "");
        return 1;
    }
    else if (rc == MEMCACHED_NOTFOUND) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_get_multi(lua_State *L)
{
    const mc_data *d;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);

    luaL_checktype(L, 2, LUA_TTABLE);

    int i;
    const int n = l_objlen(L, 2);
    const char *keys[n];
    size_t keys_size[n];

    for (i = 0; i < n; i++) {
        lua_rawgeti(L, 2, i + 1);
        keys[i] = lua_tolstring(L, -1, &keys_size[i]);
        lua_pop(L, 1);
    }

    assert(2 == lua_gettop(L));

    rc = memcached_mget(d->mc, keys, keys_size, n);

    if (rc != MEMCACHED_SUCCESS) {
        return l_error(L, rc);
    }

    char key[MEMCACHED_MAX_KEY];
    size_t key_length;
    char *value;
    size_t value_length;
    uint32_t flags;

    lua_newtable(L);

    for (;;) {
        value = memcached_fetch(d->mc, key, &key_length,
                                &value_length, &flags, &rc);
        if (rc != MEMCACHED_SUCCESS) {
            return 1;
        }

        lua_pushlstring(L, key, key_length);

        if (value != NULL) {
            DECODE_VALUE
        }
        else {
            lua_pushliteral(L, "");
        }

        lua_rawset(L, -3);
    }
}


static inline memcached_return_t
l_put(lua_State *L, memcached_set_pt f)
{
    const mc_data *d;
    const char *key;
    size_t key_length;
    const char *value;
    size_t value_length;
    time_t expiration;
    uint32_t flags;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = l_key_encode(L, d, 2, &key_length);
    expiration = luaL_optnumber(L, 4, 0);

    switch(lua_type(L, 3)) {
        case LUA_TTABLE:
            flags = FLAG_ENCODED;
            lua_rawgeti(L, LUA_REGISTRYINDEX, d->encode);
            lua_pushvalue(L, 3);
            lua_call(L, 1, 1);
            value = luaL_checklstring(L, -1, &value_length);
            break;

        case LUA_TNUMBER:
            flags = FLAG_NUMBER;
            value = lua_tolstring(L, -1, &value_length);
            break;

        case LUA_TBOOLEAN:
            flags = FLAG_BOOLEAN;
            value = lua_toboolean(L, -1) ? "1" : "0";
            value_length = 1;
            break;

        case LUA_TSTRING:
            flags = FLAG_NONE;
            value = luaL_checklstring(L, 3, &value_length);
            break;

        default:
            return luaL_error(L, "unsuported value type");
    }

    return f(d->mc, key, key_length, value, value_length,
             expiration, flags);
}


static int
l_set(lua_State *L)
{
    const memcached_return_t rc = l_put(L, memcached_set);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_add(lua_State *L)
{
    const memcached_return_t rc = l_put(L, memcached_add);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_DATA_EXISTS) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_replace(lua_State *L)
{
    const memcached_return_t rc = l_put(L, memcached_replace);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_NOTFOUND) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_append(lua_State *L)
{
    const memcached_return_t rc = l_put(L, memcached_append);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_NOTSTORED) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_prepend(lua_State *L)
{
    const memcached_return_t rc = l_put(L, memcached_prepend);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_NOTSTORED) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_delete(lua_State *L)
{
    const mc_data *d;
    const char *key;
    size_t key_length;
    time_t expiration;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = l_key_encode(L, d, 2, &key_length);
    expiration = luaL_optnumber(L, 3, 0);

    rc = memcached_delete(d->mc, key, key_length, expiration);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_NOTFOUND) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_touch(lua_State *L)
{
    const mc_data *d;
    const char *key;
    size_t key_length;
    time_t expiration;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = l_key_encode(L, d, 2, &key_length);
    expiration = luaL_checknumber(L, 3);

    rc = memcached_touch(d->mc, key, key_length, expiration);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_NOTFOUND) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static inline int
l_incr_decr(lua_State *L, memcached_incr_pt f)
{
    const mc_data *d;
    const char *key;
    size_t key_length;
    uint32_t offset;
    uint64_t value;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = l_key_encode(L, d, 2, &key_length);
    offset = luaL_optnumber(L, 3, 1);

    rc = f(d->mc, key, key_length, offset, &value);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushnumber(L, value);
        return 1;
    }
    else if (rc == MEMCACHED_NOTFOUND) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_incr(lua_State *L)
{
    return l_incr_decr(L, memcached_increment);
}


static int
l_decr(lua_State *L)
{
    return l_incr_decr(L, memcached_decrement);
}


#if LIBMEMCACHED_VERSION_HEX >= 0x01000009

static int
l_exist(lua_State *L)
{
    mc_data *d;
    const char *key;
    size_t key_length;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    key = l_key_encode(L, d, 2, &key_length);

    rc = memcached_exist(d->mc, key, key_length);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else if (rc == MEMCACHED_NOTFOUND) {
        lua_pushnil(L);
        return 1;
    }

    return l_error(L, rc);
}

#endif


static int
l_flush(lua_State *L)
{
    mc_data *d;
    time_t expiration;
    memcached_return_t rc;

    d = (mc_data *)luaL_checkudata(L, 1, MC_STATE);
    expiration = luaL_optnumber(L, 2, 0);

    rc = memcached_flush(d->mc, expiration);

    if (rc == MEMCACHED_SUCCESS) {
        lua_pushboolean(L, 1);
        return 1;
    }

    return l_error(L, rc);
}


static int
l_createmeta(lua_State *L, const char *name, const luaL_Reg *methods,
             const luaL_Reg *mt)
{
    if (!luaL_newmetatable(L, name)) {
        return 0;
    }

    l_setfuncs(L, mt);

    lua_newtable(L);
    l_setfuncs(L, methods);
    lua_setfield(L, -2, "__index");

    lua_pushliteral(L, "it is not allowed to get metatable.");
    lua_setfield(L, -2, "__metatable");

    return 1;
}


int
luaopen_libmemcached(lua_State *L)
{
    luaL_Reg methods[] = {
        { "new", l_new },
        { }
    };
    luaL_Reg state_methods[] = {
        { "close", l_close },
        { "get_behavior", l_get_behavior },
        { "set_behavior", l_set_behavior },
#if LIBMEMCACHED_VERSION_HEX >= 0x01000006
        { "set_encoding_key", l_set_encoding_key },
#endif
        { "get", l_get },
        { "get_multi", l_get_multi },
        { "set", l_set },
        { "add", l_add },
        { "replace", l_replace },
        { "append", l_append },
        { "prepend", l_prepend },
        { "delete", l_delete },
        { "touch", l_touch },
        { "incr", l_incr },
        { "decr", l_decr },
#if LIBMEMCACHED_VERSION_HEX >= 0x01000009
        { "exist", l_exist },
#endif
        { "flush", l_flush },
        { }
    };
    luaL_Reg state_mt[] = {
        { "__gc", l_gc },
        { }
    };

    if (!l_createmeta(L, MC_STATE, state_methods, state_mt)) {
        return 0;
    }

    // TODO: check
    lua_pop(L, 1);

    // module
    lua_newtable(L);

    // status
    lua_newtable(L);

    BEHAVIOR(NO_BLOCK)
    BEHAVIOR(TCP_NODELAY)
    BEHAVIOR(HASH)
    BEHAVIOR(KETAMA)
    BEHAVIOR(SOCKET_SEND_SIZE)
    BEHAVIOR(SOCKET_RECV_SIZE)
    // BEHAVIOR(CACHE_LOOKUPS)
    BEHAVIOR(SUPPORT_CAS)
    BEHAVIOR(POLL_TIMEOUT)
    BEHAVIOR(DISTRIBUTION)
    BEHAVIOR(BUFFER_REQUESTS)
    // BEHAVIOR(USER_DATA)
    BEHAVIOR(SORT_HOSTS)
    BEHAVIOR(VERIFY_KEY)
    BEHAVIOR(CONNECT_TIMEOUT)
    BEHAVIOR(RETRY_TIMEOUT)
    BEHAVIOR(KETAMA_WEIGHTED)
    BEHAVIOR(KETAMA_HASH)
    BEHAVIOR(BINARY_PROTOCOL)
    BEHAVIOR(SND_TIMEOUT)
    BEHAVIOR(RCV_TIMEOUT)
    BEHAVIOR(SERVER_FAILURE_LIMIT)
    BEHAVIOR(IO_MSG_WATERMARK)
    BEHAVIOR(IO_BYTES_WATERMARK)
    BEHAVIOR(IO_KEY_PREFETCH)
    BEHAVIOR(HASH_WITH_PREFIX_KEY)
    BEHAVIOR(NOREPLY)
    BEHAVIOR(USE_UDP)
    BEHAVIOR(AUTO_EJECT_HOSTS)
    BEHAVIOR(NUMBER_OF_REPLICAS)
    BEHAVIOR(RANDOMIZE_REPLICA_READ)
    // BEHAVIOR(CORK)
    BEHAVIOR(TCP_KEEPALIVE)
    BEHAVIOR(TCP_KEEPIDLE)
    // BEHAVIOR(LOAD_FROM_FILE)
    BEHAVIOR(REMOVE_FAILED_SERVERS)
    BEHAVIOR(DEAD_TIMEOUT)
    // BEHAVIOR(MAX)

    lua_setfield(L, -2, "behaviors");

    l_setfuncs(L, methods);

    return 1;
}
