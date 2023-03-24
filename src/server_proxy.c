// #include <arpa/inet.h>
// #include <netdb.h>
#include <signal.h>
// #include <stdio.h> // fprint
#include <stdlib.h> // exit
// #include <string.h> // strerror
// #include <sys/socket.h>
// #include <unistd.h> // fork close

#include "common.c"

int connect_host_sock(const char *, const char *);

int main(int argc, char *argv[]) {
    if(FALSE == proxy_stop_mode_check(argc, argv, SERVER) || argc < (P_CLIENT_IDX + 1)) {
        printf(P_COMMON_USAGE" (start:secret)\n", argv[0]);
        return 0;
    }

    key_init(argv[P_CLIENT_IDX]);

    int local_port = atoi(argv[P_PORT_IDX]);

    signal(SIGCHLD, sigchld_handler); // 防止子进程变成僵尸进程

    int server_sock;
    if (-1 == (server_sock = create_server_socket(local_port))) {
        exit(0);
    }

    printf("open server port(%d): %d \n", server_sock, local_port);

    proxy_save_pid(SERVER, argv[P_PORT_IDX]);
 
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
        int read_len = recv_data(client_sock, remote_host, 120, TRUE);
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

        memcpy(remote_port, &remote_host[i+1], strlen(remote_host) - i -3);
        memset(&remote_host[i], 0, 120-i);

        int remote_sock;
        if ((remote_sock = connect_host_sock(remote_host, remote_port)) < 0) {
            LOG_ERR("Cannot connect to host [%s:%s] \n", remote_host, remote_port);
            goto g_over;
        }

        memset(&remote_port[3], 0, 2);
        memcpy(remote_port, "200", 3);
        send_data(client_sock, remote_port, 3, TRUE);

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


// 连接
int connect_host_sock(const char *host, const char *port) {
    struct sockaddr_in server_addr;
    struct hostent *server;
    if(NULL == (server = gethostbyname(host))) {
        LOG_ERR("Unknown host: %s\n", host);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(atoi(port));
    return create_connect_socket(&server_addr);
}