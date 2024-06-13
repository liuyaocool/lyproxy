
#include "common.c"

// 连接
int connect_host_sock(const char *host, const char *port) {
    struct sockaddr_in server_addr;
    if(!get_sockaddr_in(&server_addr, host, port)) {
        LOG_ERR("Unknown host: %s\n", host);
        return -1;
    }
    return create_connect_socket(&server_addr);
}

int main(int argc, char *argv[]) {
    if (argc < 3){
        printf("Usage: %s <locap_port> <secret>\n", argv[0]);
        exit(0);
    }

    key_init(argv[2]);

    int local_port = atoi(argv[1]);

    signal(SIGCHLD, sigchld_handler); // 防止子进程变成僵尸进程

    int server_sock;
    if (-1 == (server_sock = create_server_socket(local_port))) {
        exit(0);
    }

    printf("open server port: %d \n", local_port);
 
    // 子进程
    // pid_t pid = fork();
    // if (pid > 0 ) {
    //     LOGERR("mporxy pid is: [%d]\n",pid);
    //     close(server_sock);
    //     return 0;
    // } else if(pid != 0) {
    //     LOGERR("Cannot daemonize\n");
    //     exit(pid);
    //     return 0;
    // }

    int client_sock;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);
        if (fork() != 0) { // 创建子进程处理客户端连接请求
            close(client_sock);
            continue;
        }
        close(server_sock);

        char remote_host[120];
        char remote_port[5];
        memset(remote_host, 0, 120);
        memset(remote_port, 0, 5);
        int read_len = recv_data(client_sock, remote_host, 120, true);
        if (0 == read_len) {
            goto g_over;
        } else if (read_len < 0) {
            LOG_ERR("read_len err2: %d \n", read_len);
            goto g_over;
        }

        int i;
        for (i = 0; i < read_len; i++) {
            if (':' == remote_host[i]) {
                break;
            }
        }

        if (0 == i || i >= read_len) {
            LOG_ERR("Connect pakage err [%s] i=%d \n", remote_host, i);
            goto g_over;
        }

        // -3: protocol`s first package ending
        memcpy(remote_port, &remote_host[i+1], strlen(remote_host) - i -3);
        memset(&remote_host[i], 0, 120-i);

        int remote_sock;
        if ((remote_sock = connect_host_sock(remote_host, remote_port)) < 0) {
            LOG_ERR("Cannot connect to host [%s:%s] \n", remote_host, remote_port);
            goto g_over;
        }

        memset(&remote_port[3], 0, 2);
        memcpy(remote_port, "200", 3);
        send_data(client_sock, remote_port, 3, true);

        // client_proxy -> remote
        fork_forward(client_sock, remote_sock, DECRYPT);
        // remote -> client_proxy
        fork_forward(remote_sock, client_sock, ENCRYPT);
        
        g_over2:
        close(remote_sock);
        g_over:
        close(client_sock);
        exit(0);
    }
    return 0;
}