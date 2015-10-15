package = 'lua-libmemcached'

version = 'scm-0'

description = {
    summary = 'An interface to the libmemcached C client.',
    homepage = 'https://github.com/akornatskyy/lua-libmemcached',
    maintainer = 'Andriy Kornatskyy <andriy.kornatskyy@live.com>',
    license = 'MIT'
}

dependencies = {
    'lua >= 5.1'
}

source = {
    url = 'git://github.com/akornatskyy/lua-libmemcached.git'
}

external_dependencies = {
    LIBMEMCACHED = {
        header = 'libmemcached/memcached.h'
    }
}

build = {
    type = 'builtin',
    modules = {
        ['libmemcached'] = {
            sources = {'src/libmemcached.c'},
            -- libmemcached 1.0.14+ requires pthread
            libraries = {'memcached', 'pthread'},
            incdirs = {'$(LIBMEMCACHED_INCDIR)'},
            libdirs = {'$(LIBMEMCACHED_LIBDIR)'}
        }
    }
}
