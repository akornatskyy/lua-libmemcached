.SILENT: env test clean qa
.PHONY: env test clean qa

ENV=$(shell pwd)/env
LUA_IMPL=
LUA_VERSION=5.1.5
PLATFORM=macosx
LUA_ROCKS_VERSION=2.2.2
LIBMEMCACHED_DIR=/opt/local


env:
	rm -rf $(ENV) ; mkdir -p $(ENV) ; \
	unset LUA_PATH ; unset LUA_CPATH ; \
	if [ "$(LUA_IMPL)" = "luajit" ] ; then \
		wget -c http://luajit.org/download/LuaJIT-$(LUA_VERSION).tar.gz \
			-O - | tar xzf - ; \
		cd LuaJIT-$(LUA_VERSION) ; \
		export MACOSX_DEPLOYMENT_TARGET=10.10 ; \
		make -s install PREFIX=$(ENV) INSTALL_INC=$(ENV)/include ; \
		ln -sf luajit-$(LUA_VERSION) $(ENV)/bin/lua ; \
		cd .. ; rm -rf luajit-$(LUA_VERSION) ; \
	else \
		wget -c http://www.lua.org/ftp/lua-$(LUA_VERSION).tar.gz \
			-O - | tar -xzf - ; \
		cd lua-$(LUA_VERSION) ; \
		make -s $(PLATFORM) install INSTALL_TOP=$(ENV) ; \
		cd .. ; rm -rf lua-$(LUA_VERSION) ; \
	fi ; \
	wget -c http://luarocks.org/releases/luarocks-$(LUA_ROCKS_VERSION).tar.gz \
		-O - | tar -xzf - ; \
	cd luarocks-$(LUA_ROCKS_VERSION) ; \
	./configure --prefix=$(ENV) --with-lua=$(ENV) --sysconfdir=$(ENV) \
		--force-config && \
	make -s build install && \
	cd .. ; rm -rf luarocks-$(LUA_ROCKS_VERSION) ; \
	for rock in busted luacov luacheck; do \
		$(ENV)/bin/luarocks --deps-mode=one install $$rock ; \
	done

debian:
	apt-get install build-essential unzip libncurses5-dev libreadline6-dev \
		libssl-dev libmemcached-dev

test:
	$(ENV)/bin/busted

lib:
	$(ENV)/bin/luarocks make LIBMEMCACHED_DIR=$(LIBMEMCACHED_DIR)

clean:
	find src/ -name '*.o' -delete
	rm -rf luacov.* luac.out *.so

qa:
	$(ENV)/bin/luacheck -q src/ spec/
