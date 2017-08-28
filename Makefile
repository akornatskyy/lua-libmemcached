.SILENT: clean env lib test qa debian rm lua luajit luarocks
.PHONY: clean env lib test qa debian rm lua luajit luarocks

ENV=$(shell pwd)/env
# lua | luajit
LUA_IMPL=lua
LUA_VERSION=5.1.5
LUAROCKS_VERSION=2.4.2
LIBMEMCACHED_DIR=/usr

ifeq (Darwin,$(shell uname -s))
  PLATFORM?=osx
else
  PLATFORM?=linux
endif

clean:
	find src/ -name '*.o' -delete ; \
	rm -rf core luacov.* luac.out *.so

env: luarocks
	for rock in busted luacov luacheck; do \
		$(ENV)/bin/luarocks --deps-mode=one install $$rock ; \
	done

lib:
	$(ENV)/bin/luarocks make LIBMEMCACHED_DIR=$(LIBMEMCACHED_DIR)

test:
	$(ENV)/bin/busted

qa:
	$(ENV)/bin/luacheck -q src/ spec/

valgrind-test:
	eval `$(ENV)/bin/luarocks path` ; \
	echo 'os.exit = function() end' > exitless-busted ; \
	echo 'require "busted.runner"({ standalone = false })' >> exitless-busted ; \
	valgrind --error-exitcode=1 --leak-check=full --gen-suppressions=all \
		$(ENV)/bin/lua exitless-busted ; \
	rm exitless-busted

debian:
	apt-get install wget build-essential unzip libncurses5-dev \
		libreadline6-dev libssl-dev libmemcached-dev valgrind memcached

rm: clean
	rm -rf $(ENV)

lua: rm
	wget -c https://www.lua.org/ftp/lua-$(LUA_VERSION).tar.gz \
		-O - | tar -xzf - && \
	cd lua-$(LUA_VERSION) && \
	sed -i.bak s%/usr/local%$(ENV)%g src/luaconf.h && \
	sed -i.bak s%./?.lua\;%./?.lua\;./src/?.lua\;%g src/luaconf.h && \
	unset LUA_PATH && unset LUA_CPATH && \
	make -s $(PLATFORM) install INSTALL_TOP=$(ENV) && \
	cd .. && rm -rf lua-$(LUA_VERSION)

luajit: rm
	wget -c https://github.com/LuaJIT/LuaJIT/archive/v$(LUA_VERSION).tar.gz \
		-O - | tar xzf - && \
  	cd LuaJIT-$(LUA_VERSION) && \
  	sed -i.bak s%/usr/local%$(ENV)%g src/luaconf.h && \
	sed -i.bak s%./?.lua\"%./?.lua\;./src/?.lua\"%g src/luaconf.h && \
	export MACOSX_DEPLOYMENT_TARGET=10.10 && \
	export PATH=/sbin:$$PATH && \
	unset LUA_PATH && unset LUA_CPATH && \
    make -s install PREFIX=$(ENV) INSTALL_INC=$(ENV)/include && \
	ln -sf luajit-$(LUA_VERSION) $(ENV)/bin/lua && \
	cd .. && rm -rf LuaJIT-$(LUA_VERSION)

luarocks: $(LUA_IMPL)
	wget -qc https://luarocks.org/releases/luarocks-$(LUAROCKS_VERSION).tar.gz \
		-O - | tar -xzf - && \
	cd luarocks-$(LUAROCKS_VERSION) && \
	./configure --prefix=$(ENV) --with-lua=$(ENV) --force-config && \
	make -s build install && \
	cd .. && rm -rf luarocks-$(LUAROCKS_VERSION)
