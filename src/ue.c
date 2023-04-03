#include <sys/epoll.h>

#include "u.c"

#define EP_EV_LEN 2000

struct ep_tree_data
{
    int from_fd;
    int to_fd;
    enum CryptMode mode;
    // 数据处理函数
    // void (*handler)(int ep_fd, struct epoll_event);
};

// int add_to_epoll(int ep_fd, int from_fd, int to_fd, enum CryptMode mode
    // ,void (*handler)(int ep_fd, struct epoll_event)
// ){}

int add_to_epoll(int ep_fd, int from_fd, int to_fd, enum CryptMode mode) {
    // 如果这里不申请内存 方法结束即释放 则不会被copy 导致接受不到数据
    struct ep_tree_data *epd = (struct ep_tree_data *) malloc(sizeof(struct ep_tree_data));
    epd->from_fd = from_fd;
    epd->to_fd = to_fd;
    epd->mode = mode;
    struct epoll_event a = {0, {0}}; // event 会被copy
    a.events = EPOLLIN;
    a.data.ptr = epd;
    int ep_res = epoll_ctl(ep_fd, EPOLL_CTL_ADD, from_fd, &a);
    if(0 != ep_res) free(epd);
    return ep_res;
}

int del_from_epoll(int ep_fd, struct ep_tree_data* epd) {
    int ep_res = epoll_ctl(ep_fd, EPOLL_CTL_ADD, epd->from_fd, NULL);
    if(0 == ep_res) free(epd);
    return ep_res;
}