# Test host della logica senza dipendenze ESP-IDF.
ROOT := ../..
SRCS := test_main.c $(ROOT)/components/hid/hid_layout.c $(ROOT)/components/hid/layouts_data.c \
        $(ROOT)/components/input/gesture.c $(ROOT)/apps/netscan/netscan_util.c $(ROOT)/apps/webfiles/secrets.c
INC := -I$(ROOT)/components/hid/include -I$(ROOT)/components/input/include -I$(ROOT)/apps/netscan -I$(ROOT)/apps/webfiles

test: $(SRCS)
	$(CC) -std=c11 -Wall -Wextra -o $(CURDIR)/host_test $(INC) $(SRCS)
	$(CURDIR)/host_test

.PHONY: test
