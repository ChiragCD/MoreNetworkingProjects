coordinator: coordinator.c node.c
	gcc coordinator.c node.c -o coordinator
tftpserver: tftpserver.c
	gcc tftpserver.c -o tftpserver
fastertraceroute:
	gcc fastertraceroute.c -o fastertraceroute -lpthread
findLongestCommonPath:
	gcc findLongestCommonPath.c -o findLongestCommonPath

