idf_component_register(SRCS "webtheme.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES websrv)

# css, js e svg compressi in fase di build come le pagine delle app; i font
# woff2 sono già compressi e si incorporano così come sono.
set(WEB ${CMAKE_CURRENT_SOURCE_DIR}/web)
set(GZ_FILES "")
foreach(f theme.css theme.js grillo.svg)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/${f}.gz)
    add_custom_command(OUTPUT ${out}
                       COMMAND ${PYTHON} -c "import gzip,sys;open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" ${WEB}/${f} ${out}
                       DEPENDS ${WEB}/${f}
                       VERBATIM)
    list(APPEND GZ_FILES ${out})
endforeach()
add_custom_target(webtheme_files DEPENDS ${GZ_FILES})
add_dependencies(${COMPONENT_LIB} webtheme_files)
foreach(out ${GZ_FILES})
    target_add_binary_data(${COMPONENT_LIB} ${out} BINARY)
endforeach()
target_add_binary_data(${COMPONENT_LIB} ${WEB}/fonts/silkscreen.woff2 BINARY)
target_add_binary_data(${COMPONENT_LIB} ${WEB}/fonts/iosevka.woff2 BINARY)
