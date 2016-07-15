all:
	make
	make -f test_server.mk
	make -f test_client.mk
