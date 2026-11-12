idf_component_register(SRCS "webinjector.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES injector_core clock esp_timer ui led usbdev net config websrv webjson webtheme webui)

# The page is compressed at build time, like the Web HID one
set(WEB_HTML ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html)
set(WEB_GZ ${CMAKE_CURRENT_BINARY_DIR}/webinjector.html.gz) # name differs from the Web HID page: the same binary symbol would not link
add_custom_command(OUTPUT ${WEB_GZ}
                   COMMAND ${PYTHON} -c "import gzip,sys;open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" ${WEB_HTML} ${WEB_GZ}
                   DEPENDS ${WEB_HTML}
                   VERBATIM)
add_custom_target(webinjector_page DEPENDS ${WEB_GZ})
add_dependencies(${COMPONENT_LIB} webinjector_page)
target_add_binary_data(${COMPONENT_LIB} ${WEB_GZ} BINARY)
