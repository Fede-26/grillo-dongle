# Lua 5.4.9 senza modifiche ai sorgenti (vedi README.md).
file(GLOB LUA_SRCS "lua/*.c")
idf_component_register(SRCS ${LUA_SRCS}
                       INCLUDE_DIRS "lua")
# Il codice upstream genera alcuni warning con le opzioni di ESP-IDF:
# non li correggiamo per non divergere dai sorgenti ufficiali.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-unused-but-set-variable -Wno-maybe-uninitialized)
