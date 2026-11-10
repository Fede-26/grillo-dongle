idf_component_register(SRCS "webfiles.c" "secrets.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES injector_core luart lua clock ui led storage usbdev net config websrv webjson webtheme webui)

# La pagina viene compressa in fase di build, come quella del Web HID
set(WEB_HTML ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html)
set(WEB_GZ ${CMAKE_CURRENT_BINARY_DIR}/webfiles.html.gz) # nome unico: il simbolo binario deriva dal nome del file
add_custom_command(OUTPUT ${WEB_GZ}
                   COMMAND ${PYTHON} -c "import gzip,sys;open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" ${WEB_HTML} ${WEB_GZ}
                   DEPENDS ${WEB_HTML}
                   VERBATIM)
add_custom_target(webfiles_page DEPENDS ${WEB_GZ})
add_dependencies(${COMPONENT_LIB} webfiles_page)
target_add_binary_data(${COMPONENT_LIB} ${WEB_GZ} BINARY)
