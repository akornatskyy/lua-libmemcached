local libmemcached = require 'libmemcached'
local json = require 'dkjson'
local assert, describe, it = assert, describe, it


local function count_refs()
    local m = 0
    local n = 0
    local r = debug.getregistry()
    local i = r[0]
    for k, v in pairs(r) do
        if type(k) == 'number' then
            m = m + 1
        end
    end
    while i do
        i = r[i]
        n = n + 1
    end
    return m - n
end


describe('libmemcached lifecycle', function()
    describe('new', function()
        it('1st argument is a connection string', function()
            local c = count_refs()
            assert.has_error(function()
                libmemcached.new()
            end,
            'bad argument #1 to \'new\' (string expected, got no value)')
            assert.equals(c, count_refs())
        end)

        it('2nd argument is a table', function()
            local c = count_refs()
            assert.has_error(function()
                libmemcached.new('')
            end,
            'bad argument #2 to \'new\' (table expected, got no value)')
            assert.equals(c, count_refs())
        end)

        it('2nd argument table encode is missing', function()
            local c = count_refs()
            assert.has_error(function()
                libmemcached.new('', {})
            end,
            'bad argument #2 (\'encode\' function is missing)')
            assert.equals(c, count_refs())
        end)

        it('2nd argument table decode is missing', function()
            local c = count_refs()
            assert.has_error(function()
                libmemcached.new('', {encode = function() end})
            end,
            'bad argument #2 (\'decode\' function is missing)')
            assert.equals(c, count_refs())
        end)

        it('invalid connection options', function()
            local c = count_refs()
            -- fails with 1.0.[3-7]
            assert.has_error(function()
                libmemcached.new('', json)
            end,
            'cannot allocate memcached object')
            assert.equals(c, count_refs())
        end)

        it('3rd argument key_encode is optional', function()
            assert(libmemcached.new('--server=127.0.0.1', json))
        end)

        it('3rd argument must be a function', function()
            local c = count_refs()
            assert.has_error(function()
                libmemcached.new('--server=127.0.0.1', json, nil)
            end,
            'bad argument #3 (\'key_encode\' must be a function)')
            assert.equals(c, count_refs())
        end)

        it('3rd argument is a function', function()
            assert(libmemcached.new('--server=127.0.0.1', json, function(s)
                return s
            end))
        end)
    end)

    describe('flush', function()
        it('empties cache', function()
            local c = assert(libmemcached.new('--server=127.0.0.1:11211',
                                              json))
            assert.is_true(c:set('x', 100))
            assert.equals(100, c:get('x'))
            c:flush()
            assert.is_nil(c:get('x'))
        end)
    end)

    describe('gc', function()
        it('cant call gc from Lua code', function()
            local c = assert(libmemcached.new('--server=127.0.0.1', json))
            assert.has_error(function()
                c:__gc()
            end)
        end)

        it('frees registry entries', function()
            collectgarbage()
            local N = 100
            local c = count_refs()
            local t = {}
            for i = 1, N do
                t[i] = assert(libmemcached.new('--server=127.0.0.1', json))
            end
            t = {}
            collectgarbage()
            assert.equal(c, count_refs())

            c = count_refs()
            local key_encode = function(s) return s end
            for i = 1, N do
                t[i] = assert(libmemcached.new('--server=127.0.0.1', json, key_encode))
            end
            t = nil
            collectgarbage()
            assert.equal(c, count_refs())
            assert.is_nil(t)
        end)
    end)

    describe('close', function()
        it('releases instance', function()
            local c = assert(libmemcached.new('--server=127.0.0.1', json))
            c:close()
            local r, err = c:get('s')
            assert.is_nil(r)
            assert.equals('INVALID ARGUMENTS', err)
        end)

        it('safe to call twice', function()
            local c = assert(libmemcached.new('--server=127.0.0.1', json))
            assert.is_true(c:close())
            assert.is_false(c:close())
        end)

        it('frees registry entries', function()
            local N = 100
            local c = count_refs()
            local t = {}
            for i = 1, N do
                t[i] = assert(libmemcached.new('--server=127.0.0.1', json))
            end
            for i = 1, N do
                t[i]:close()
            end
            assert.equal(c, count_refs())

            c = count_refs()
            local key_encode = function(s) return s end
            for i = 1, N do
                t[i] = assert(libmemcached.new('--server=127.0.0.1', json, key_encode))
            end
            for i = 1, N do
                t[i]:close()
            end
            assert.equal(c, count_refs())
        end)
    end)
end)

local function describe_basic_commands(c)
    local samples = {
        t_empty = {},
        t = {message = 'hello'},
        s_empty = '',
        s = 'hello',
        n_0 = 0,
        n_100 = 100,
        nn_100 = -100,
        n_5_5 = 5.5,
        nn_5_5 = -5.5,
        b_true = true,
        b_false = false,
    }

    for key, value in pairs(samples) do
        assert(c:set(key, value))
    end

    describe('get', function()
        for key, value in pairs(samples) do
            it('decodes ' .. key .. ' sample', function()
                assert.same(value, c:get(key))
            end)
        end

        it('1st argument is a string', function()
            assert.has_error(function()
                c:get(nil)
            end,
            'bad argument #1 to \'get\' (string expected, got nil)')
        end)

        it('returns nil if not found', function()
            assert.is_nil(c:get('unknown'))
        end)
    end)

    describe('get multi', function()
        it('decodes samples', function()
            local keys = {}
            for key in pairs(samples) do
                keys[#keys+1] = key
            end
            assert.same(samples, c:get_multi(keys))
        end)

        it('1st argument is a table', function()
            assert.has_error(function()
                c:get_multi(nil)
            end,
            'bad argument #1 to \'get_multi\' (table expected, got nil)')
        end)

        it('returns nil for keys not found', function()
            local r = c:get_multi({'x1', 'x2', 'x3'})
            assert.is_nil(r.x1)
            assert.is_nil(r.x2)
            assert.is_nil(r.x3)
        end)
    end)

    describe('set', function()
        it('1st argument is a string', function()
            assert.has_error(function()
                c:set()
            end,
            'bad argument #1 to \'set\' (string expected, got no value)')
        end)

        it('an error to store nil', function()
            assert.has_error(function()
                c:set('nil', nil)
            end, 'unsuported value type')
        end)

        it('3st argument is a number', function()
            assert.has_error(function()
                c:set('s', '', 'x')
            end,
            'bad argument #3 to \'set\' (number expected, got string)')
        end)

        it('supports keys up to 250 characters long', function()
            assert(c:set(('x'):rep(250), 1))
        end)

        it('an error to use long keys if key_encode is not defined', function()
            assert.has_error(function()
                assert(c:set(('x'):rep(251), 1))
            end, 'key is too long')
        end)

        it('calls key_encode function for long keys', function()
            local called = false
            local key_encode = function(key)
                called = true
                return key:sub(1, 250)
            end
            c = assert(libmemcached.new('--server=127.0.0.1:11211',
                                        json, key_encode))
            assert(c:set(('x'):rep(251), 1))
            assert(called)
        end)
    end)

    describe('add', function()
        it('returns nil if key exists', function()
            assert.is_nil(c:add('s', ''))
        end)

        it('returns true on success', function()
            local key = 'key-add'
            c:delete(key)
            assert.is_true(c:add(key, ''))
        end)
    end)

    describe('replace', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:replace('not-found', ''))
        end)

        it('returns true on success', function()
            local key = 'replace-key'
            assert.is_true(c:set(key, 0))
            assert.equals(0, c:get(key))
            assert.is_true(c:replace(key, 1))
            assert.equals(1, c:get(key))
        end)
    end)

    describe('append', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:append('not-found', 'x'))
        end)

        it('returns true on success', function()
            local key = 'append-key'
            assert.is_true(c:set(key, '1;'))
            assert.equals('1;', c:get(key))
            assert.is_true(c:append(key, '2;'))
            assert.equals('1;2;', c:get(key))
        end)
    end)

    describe('prepend', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:prepend('not-found', 'x'))
        end)

        it('returns true on success', function()
            local key = 'prepend-key'
            assert.is_true(c:set(key, '2;'))
            assert.equals('2;', c:get(key))
            assert.is_true(c:prepend(key, '1;'))
            assert.equals('1;2;', c:get(key))
        end)
    end)

    describe('delete', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:delete('not-found'))
        end)

        it('returns true on success', function()
            local key = 'delete-key'
            assert.is_true(c:set(key, 'x'))
            assert.is_true(c:delete(key))
            assert.is_nil(c:get(key))
        end)
    end)

    describe('touch', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:touch('not-found', 100))
        end)

        it('returns true on success', function()
            local key = 'touch-key'
            assert.is_true(c:set(key, 'x', 10))
            assert.is_true(c:touch(key, 100))
            assert.equals('x', c:get(key))
        end)
    end)

    describe('incr', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:incr('not-found'))
        end)

        it('returns number on success', function()
            local key = 'incr-key'
            assert.is_true(c:set(key, 0))
            assert.equals(1, c:incr(key, 1))
            assert.equals(8, c:incr(key, 7))
            assert.equals(18, c:incr(key, 10))
        end)
    end)

    describe('decr', function()
        it('returns nil if key not found', function()
            assert.is_nil(c:decr('not-found'))
        end)

        it('returns number on success', function()
            local key = 'decr-key'
            assert.is_true(c:set(key, 100))
            assert.equals(99, c:decr(key, 1))
            assert.equals(92, c:decr(key, 7))
            assert.equals(82, c:decr(key, 10))
        end)
    end)

    if c.exist then
        describe('exist', function()
            it('returns nil if key not found', function()
                assert.is_nil(c:exist('not-found'))
            end)

            it('returns true on success', function()
                assert.is_true(c:exist('s'))
            end)
        end)
    end
end

describe('libmemcached commands (text protocol)', function()
    local c = assert(libmemcached.new('--server=127.0.0.1', json))

    describe_basic_commands(c)
end)

describe('libmemcached commands (binary protocol)', function()
    local c = assert(libmemcached.new(
        '--server=127.0.0.1 --binary-protocol', json))

    describe_basic_commands(c)
end)

describe('libmemcached behavior', function()
    local c = assert(libmemcached.new('--server=127.0.0.1', json))

    describe('get', function()
        it('1st argument is a number', function()
            assert.has_error(function()
                c:get_behavior()
            end,
            'bad argument #1 to \'get_behavior\' (number expected, ' ..
            'got no value)')
        end)
    end)

    describe('set', function()
        it('1st argument is a number', function()
            assert.has_error(function()
                c:set_behavior()
            end,
            'bad argument #1 to \'set_behavior\' (number expected, ' ..
            'got no value)')
        end)

        it('2nd argument is a number', function()
            assert.has_error(function()
                c:set_behavior(libmemcached.behaviors.NO_BLOCK)
            end,
            'bad argument #2 to \'set_behavior\' (number expected, ' ..
            'got no value)')
        end)
    end)

    it('default can be set back', function()
        for name, i in pairs(libmemcached.behaviors) do
            local d = c:get_behavior(i)
            assert.is_true(c:set_behavior(i, d))
            assert.equals(d, c:get_behavior(i))
        end
    end)
end)

describe('libmemcached data encryption', function()
    local c = assert(libmemcached.new('--server=127.0.0.1', json))
    if c.set_encoding_key then
        -- binary protocol does not support encryption
        assert(c:set_encoding_key('secret'))
        c:set('x', 'test')

        -- Segmentation fault
        -- c:set('x', '')
        -- describe_basic_commands(c)

        it('can read encrypted data back', function()
            assert.equals('test', c:get('x'))
        end)

        it('fails to decode with invalid key', function()
            c:set_encoding_key('wrong')
            local ok, err = c:get('x')
            assert.is_nil(ok)
            assert(err:find('decrypt') or err:find('FAIL'))
        end)
    end
end)
