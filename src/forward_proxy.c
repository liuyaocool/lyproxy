#include "common.c"

int main(int argc, char *argv[]) {
    if (argc < 4){
        printf("Usage: %s <locap_port> <forward_ip/domain> <forward_port>\n", argv[0]);
        exit(0);
    }

    int local_port = atoi(argv[1]);
    
    struct sockaddr_in forward_addr;
    get_sockaddr_in(&forward_addr, argv[2], argv[3]);

    signal(SIGCHLD, sigchld_handler); // 防止子进程变成僵尸进程

    int server_sock;
    if (-1 == (server_sock = create_server_socket(local_port))) {
        exit(0);
    }

    printf("open server port: %d, pid: %d\n", local_port, getpid());

    int client_sock;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);
        if (fork() == 0) { // 创建子进程处理客户端连接请求
            break;
        }
        close(client_sock); // main thread continue accept
        // LOG("%d accept continue\n", client_sock);
    }

    // LOG("%d start\n", client_sock);
    close(server_sock);

    int forward_sock;
    if ((forward_sock = create_connect_socket(&forward_addr)) < 0) {
        LOG("Cannot connect to host [%s:%s] \n", argv[2], argv[3]);
        goto g_over;
    }
    // client_proxy -> forward server
    fork_forward(client_sock, forward_sock, NONE);
    // forward server -> client_proxy
    fork_forward(forward_sock, client_sock, NONE);
    
    close(forward_sock);
    g_over:
    close(client_sock);
    // LOG("%d close \n", client_sock);
    exit(0);

    return 0;
}

