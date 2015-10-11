# lua-libmemcached

[![Build Status](https://travis-ci.org/akornatskyy/lua-libmemcached.svg?branch=master)](https://travis-ci.org/akornatskyy/lua-libmemcached)

An interface to the [libmemcached](http://libmemcached.org) C client for the
[Lua](http://www.lua.org/) programming language.

# Installation

```sh
luarocks install --server=http://luarocks.org/dev lua-libmemcached LIBMEMCACHED_DIR=/opt/local
```

# Usage

```lua
local libmemcached = require 'libmemcached'
```

### new(options, encoder [, key_encode])

Creates a new instance of *memcached* client.
The *[options][1]* string is used to configure server(s)
connection and behavior; *encoder*  - a table with two functions
*encode* and *decode*; *key_encode* - a hash function for keys.

Example for *cjson*:

```lua
local encoder = require 'cjson'
```

Example for *MessagePack*:

```lua
local mp = require 'MessagePack'
local encoder = {encode = mp.pack, decode = mp.unpack}
```

Example for key encode:

```lua
local crypto = require 'crypto'

-- for text protocol
local key_encode = function(s)
    return crypto.digest('sha1', s)
end

-- for binary protocol
local key_encode = function(s)
    return crypto.digest('sha1', s, true)
end
```

Example using local server with binary protocol:

```
local c = libmemcached.new(
    '--server=127.0.0.1:11211 --binary-protocol',
    encoder, key_encode)
```
See more configuration [options][1].

[1]: http://docs.libmemcached.org/libmemcached_configuration.html#description

### add(key, value[, expiration])
### set(key, value[, expiration])
### replace(key, value[, expiration])

Used to store information on the server. Keys are currently
limited to 250 characters unless *key_encode* hash function
is used; *value* can be of type string, number, boolean or
table.

```lua
c:set('key', 'Hello World!', 100)
```

Returns `true` on success or `nil` if key was not found.

### get(key)

Used to fetch a single value from the server.

```lua
c:get('key')
```

Returns `nil` if key was not found.

### get_multi(keys)

Used to select multiple keys at once.

```lua
c:get_multi({'key', 'key2'})
```

Returns a *table*.

### append(key, value)
### prepend(key, value)

Used to modify value by prepending or appending a string
value to an existing one stored by memcached server.

### delete(key)

Used to delete a key.

### touch(key, expiration)

Used to update the expiration time on an existing key.

### incr(key[, offset])
### decr(key[, offset])

Increment or decrement keys (overflow and underflow are not detected), the
value is then returned.

### exist(key)

Check to see if a key exists.

### flush([expiration])

Used to wipe clean the contents of memcached servers. It will
either do this immediately or expire the content based on the
expiration time passed.

# Setup

Install development dependencies:

```sh
make env
make test
eval "$(env/bin/luarocks path --bin)"
```
