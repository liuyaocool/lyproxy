#include <signal.h>
#include <stdlib.h> // exit
#include <sys/epoll.h>

#include "common.c"

#define EP_EV_LEN 2000

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


void client_conn(int ep_fd, struct epoll_event ev) {

}

void accept_handler(int ep_fd, struct epoll_event ev) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    struct ep_tree_data *ep_data = (struct ep_tree_data *)ev.data.ptr;

    int client_sock = accept(ep_data->from_fd, (struct sockaddr*)&client_addr, &addrlen);
    if(-1 == client_sock) {
        int ep_res = epoll_ctl(ep_fd, EPOLL_CTL_DEL, ep_data->from_fd, NULL);
        free(ep_data);
        perror("accept error");
        return;
    }
    add_to_epoll(ep_fd, client_sock, -1, DECRYPT, client_conn);
}


int main(int argc, char *argv[]) {
    if (argc < 4){
        printf("Usage: %s <locap_port> <process_num> <secret>\n", argv[0]);
        return 1;
    }

    int server_sock, fork_id;
    int local_port = atoi(argv[1]);
    int process_num = atoi(argv[2]);

    key_init(argv[3]);

    signal(SIGCHLD, sigchld_handler); // 防止子进程变成僵尸进程

    if (-1 == (server_sock = create_server_socket(local_port, FALSE))) {
        perror("create server socket error");
        return 1;
    }

    printf("open server port: %d \n", local_port);

    for (argc = 0; argc < process_num && 0 != (fork_id = fork()); argc++) {
        if (fork_id == -1) {
            perror("create child process error");
            continue;
        }
    }
    int client_sock, evs_len, ep_fd = epoll_create(EP_EV_LEN);

    // if(0 != add_to_epoll(ep_fd, server_sock, -1, NONE, accept_handler)) {
    //     perror("sever socket add to epoll error");
    //     return 1;
    // }

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    struct epoll_event evs[EP_EV_LEN];
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);
        printf("%d accept %d\n", getpid(), client_sock);
        if(-1 != client_sock) {
            add_to_epoll(ep_fd, client_sock, -1, DECRYPT, client_conn);
        }
        evs_len = epoll_wait(ep_fd, &evs, EP_EV_LEN, -1);
        for (argc = 0; argc < evs_len; argc++) {
            ((struct ep_tree_data *)evs[argc].data.ptr)->handler(ep_fd, evs[argc]);
        }
    }
    return 0;
    err_ep1:
}