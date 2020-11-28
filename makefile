coordinator: coordinator.c node.c
	gcc coordinator.c node.c -o coordinator
tftpserver: tftpserver.c
	gcc tftpserver.c -o tftpserver
