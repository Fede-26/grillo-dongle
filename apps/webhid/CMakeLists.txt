idf_component_register(SRCS "webhid.c" "qr.c" "web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES ui led usbdev hid net config esp_http_server esp_timer lwip)

# La pagina viene compressa in fase di build: in flash resta solo il gzip,
# che il browser decomprime da sé (Content-Encoding: gzip).
set(WEB_HTML ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html)
set(WEB_GZ ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
add_custom_command(OUTPUT ${WEB_GZ}
                   COMMAND ${PYTHON} -c "import gzip,sys;open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" ${WEB_HTML} ${WEB_GZ}
                   DEPENDS ${WEB_HTML}
                   VERBATIM)
add_custom_target(webhid_page DEPENDS ${WEB_GZ})
add_dependencies(${COMPONENT_LIB} webhid_page)
target_add_binary_data(${COMPONENT_LIB} ${WEB_GZ} BINARY)
