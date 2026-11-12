# Lua 5.4.9 with unmodified sources (see README.md).
file(GLOB LUA_SRCS "lua/*.c")
idf_component_register(SRCS ${LUA_SRCS}
                       INCLUDE_DIRS "lua")
# The upstream code triggers some warnings with the ESP-IDF options:
# we do not fix them, so as not to diverge from the official sources.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-unused-but-set-variable -Wno-maybe-uninitialized)
