#include <signal.h>

#include "ue.c"

/**
 * server socket 注册到多进程上 不会惊群（即唤醒所有）
 * server socket 注册到多个epoll上 还是会惊群
 */

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

int client_conn(int ep_fd, struct ep_tree_data* ep_d) {
    char remote_host[120], remote_port[5];
    int read_len, i;
    memset(remote_host, 0, 120);
    memset(remote_port, 0, 5);

    read_len = recv_data(ep_d->from_fd, remote_host, 120, TRUE);
    if (0 == read_len) {
        goto err1;
    } else if (read_len < 0) {
        LOG_ERR("read_len err: %d \n", read_len);
        goto err1;
    }

    for (i = 0; i < read_len; i++) {
        if (':' == remote_host[i]) {
            break;
        }
    }
    if (0 == i || i >= read_len) {
        LOG_ERR("Connect pakage err [%s] i=%d \n", remote_host, i);
        goto err1;
    }

    memcpy(remote_port, &remote_host[i+1], strlen(remote_host) - i -3);
    memset(&remote_host[i], 0, 120-i);

    // 这里连接失败会阻塞整个进度
    if ((i = connect_host_sock(remote_host, remote_port)) < 0) {
        LOG_ERR("Cannot connect to host [%s:%s] \n", remote_host, remote_port);
        goto err1;
    }
    config_non_block(i);

    memset(&remote_port[3], 0, 2);
    memcpy(remote_port, "200", 3);
    send_data(ep_d->from_fd, remote_port, 3, TRUE);

    ep_d->to_fd = i;

    if(0 != add_to_epoll(ep_fd, ep_d->to_fd, ep_d->from_fd, ENCRYPT)) {
        goto err2;
    };

    return 0;

    err2:
    // shutdown(ep_d->to_fd, SHUT_RDWR); 
    LOG_ERR("%d shutdown 1: %s\n", ep_d->from_fd, remote_host);
    close(ep_d->to_fd);
    err1:
    del_from_epoll(ep_fd, ep_d);
    // shutdown(ep_d->from_fd, SHUT_RDWR); 
    LOG_ERR("%d shutdown 2: %s\n", ep_d->from_fd, remote_host);
    close(ep_d->from_fd);
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 4){
        printf("Usage: %s <locap_port> <process_num> <secret>\n", argv[0]);
        return 1;
    }

    int fork_id,
        local_port = atoi(argv[1]),
        process_num = atoi(argv[2]);

    key_init(argv[3]);

    signal(SIGCHLD, sigchld_handler); // 防止子进程变成僵尸进程

    printf("open server port: %d \n", local_port);

    for (argc = 0; argc < process_num && 0 != (fork_id = fork()); argc++) {
        if (fork_id == -1) {
            perror("create child process error");
            continue;
        }
    }

    int server_sock, client_sock, evs_len, ep_fd;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    struct epoll_event evs[EP_EV_LEN];
    char buf[BUF_SIZE];
    struct ep_tree_data* ep_d;
    enum BOL if_recv_de, if_send_en;

    if (-1 == (server_sock = create_server_socket(local_port, FALSE))) {
        perror("create server socket error");
        return 1;
    }
    ep_fd = epoll_create(EP_EV_LEN);
    add_to_epoll(ep_fd, server_sock, -1, NONE);

    for (;;) {
        evs_len = epoll_wait(ep_fd, evs, EP_EV_LEN, 200);
        for (argc = 0; argc < evs_len; argc++) {
            struct ep_tree_data* ep_d = (struct ep_tree_data *)evs[argc].data.ptr;
            // accept
            if(ep_d->from_fd == server_sock) {
                client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);
                if(-1 != client_sock) {
                    config_non_block(client_sock);
                    if(0 != add_to_epoll(ep_fd, client_sock,  -1, DECRYPT)) {
                        LOG_ERR("client[%d] add to epoll fail\n", client_sock);
                    };
                }
                continue;
            }
            if(-1 == ep_d->to_fd) {
                client_conn(ep_fd, ep_d);
                continue;
            }
            switch (ep_d->mode) {
                case ENCRYPT: if_recv_de = FALSE; if_send_en = TRUE; break;
                case DECRYPT: if_recv_de = TRUE; if_send_en = FALSE; break;
                default: if_recv_de = FALSE; if_send_en = FALSE;
            }
            while ((client_sock = recv_data(ep_d->from_fd, buf, BUF_SIZE, if_recv_de)) > 0) {
                send_data(ep_d->to_fd, buf, client_sock, if_send_en);
            }
            if(0 == client_sock) {
                close(ep_d->from_fd); 
                // shutdown(ep_d->from_fd, SHUT_RDWR); 
                continue;
            }
            if (errno != EAGAIN) {
                close(ep_d->from_fd); 
                // shutdown(ep_d->from_fd, SHUT_RDWR); 
            }
       }
    }
    return 0;
}