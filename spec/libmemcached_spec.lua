local libmemcached = require 'libmemcached'
local assert, describe, it = assert, describe, it

describe('libmemcached', function()
    local c = assert(libmemcached.new('--server=127.0.0.1:11211', {
        encode = function(t)
            return t
        end,
        decode = function(t)
            return t
        end
    }))
    c:set('skey', 'Hello')

    describe('get', function()
        it('get', function()
            assert.equals('Hello', c:get('skey'))
        end)
    end)
end)
