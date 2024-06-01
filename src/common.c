#include <stdio.h>
#include <stdlib.h> // atoi exit
#include <sys/socket.h>
#include <netinet/in.h> // sockaddr_in sizeof
#include <arpa/inet.h> // htons inet_addr
#include <errno.h>
// #include <sys/wait.h>
#include <pthread.h>
#include <netdb.h> // gethostbyname
#include <string.h> // strerror
#include <unistd.h> // fork close
#include <stdbool.h>
#include <signal.h>

#ifdef __APPLE__

#else
#include <wait.h>
#endif

#define BOL_TRUE 1
#define BOL_FALSE 0
#define BUF_SIZE 8192 // 16*n
#define LOG(fmt...)  do { fprintf(stderr,"[%s %s - %s (%d)] ",__DATE__,__TIME__, strerror(errno), getpid()); fprintf(stderr, ##fmt); } while(0)
#define LOG_ERR(fmt...)  do { fprintf(stderr,"[%s %s - %s (%d)] ",__DATE__,__TIME__, strerror(errno), getpid()); fprintf(stderr, ##fmt); } while(0)
#define IS_HTTP_CRLF(buf, pos) ('\r' == buf[pos] && '\n' == buf[pos+1])

enum CryptMode {
    DECRYPT,
    ENCRYPT,
    NONE
};

enum BOL { TRUE, FALSE };

void print_num(char *buf, int len) {
    int i;
    len = strlen(buf);
    for (i = 0; i < len; i++)
    {
        printf("%d ", buf[i]);
    }
    printf("\n");
}

static char key_en[256], key_de[256];

int hexToInt(unsigned char c) {
    if (c > 47 && c < 58) {
        return c - 48;
    }
    if (c > 96 && c < 103) {
        return c - 97 + 10;
    }
    return -1;    
}

void key_init(char *secret) {
    int i;
    for (i = 0; i < 256; i++) {
        key_en[i] = hexToInt((unsigned char) secret[i*2]) * 16 
            + hexToInt((unsigned char) secret[i*2+1]);
    }
    for (i = 0; i < 256; i++) {
        key_de[(unsigned char) key_en[i]] = i;
    }
}

void encode_map(char *buf, int buf_len, enum CryptMode mode) {
    char *key;
    switch (mode) {
        case ENCRYPT: key = key_en; break;
        case DECRYPT: key = key_de; break;
        default: return;
    }
    int i;
    // printf("%s(%d vs %d): mode=%d\n", buf, buf_len, i, mode);
    for (i = 0; i < buf_len; i++) {
        buf[i] = key[(unsigned char) buf[i]];
    }
    // printf("%s(%d vs %d): end mode=%d\n", buf, buf_len, i, mode);
}


int create_server_socket(int port) {
    int sock_fd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
     //创建套接字
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        LOG_ERR("%d socket(%d) %d\n", sock_fd, port, sock_fd);
        return sock_fd;
    }
    //将套接字设置为允许重复使用本机地址或者为设置为端口复用
    int on = 1;
    if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        LOG_ERR("%d setsockopt(%d) %d\n", sock_fd, port, sock_fd);
        goto err_over;
    }
    
    // #include <fcntl.h>
    // set non block    
    // int set_non_block = fcntl(sock_fd, F_SETFL, O_RDWR|O_NONBLOCK);
    // if (0 != set_non_block)
    // {
        // LOG_ERR("%d fail set non block with port %d\n", sock_fd, port);
    // }
   
    //填充服务器网络信息结构体
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; //监听所有ip，值为0 不需要转换字节序
    server_addr.sin_port = htons(port);
    //将套接字与服务器网络信息结构体绑定
    if(bind(sock_fd, (struct sockaddr *)&server_addr, addr_len) < 0)
    {
        LOG_ERR("%d bind(%d) %d\n", sock_fd, port, sock_fd);
        goto err_over;
    }
    //将套接字设置为被动监听状态
    if(listen(sock_fd, 5) < 0)
    {
        LOG_ERR("%d listen(%d) %d", sock_fd, port, sock_fd);
        goto err_over;
    }
    return sock_fd;

    err_over:
    shutdown(sock_fd, SHUT_RDWR);
    return -1;
}

// 指针型的参数少了拷贝到自己栈帧的过程
int create_connect_socket(const struct sockaddr_in *server_addr) {
    // .的优先级大于*
    int sock_fd;
    socklen_t addrlen = sizeof(*server_addr);
    // 创建套接字
    if((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        LOG_ERR("%d connect %s:%d >utils.c:60<", sock_fd, inet_ntoa((*server_addr).sin_addr), ntohs(server_addr->sin_port));
        return sock_fd;
    }
    // 发送客户端连接请求
    if (connect(sock_fd, (struct sockaddr *) server_addr, addrlen) == -1) {
        LOG_ERR("%d connect %s:%d", sock_fd, inet_ntoa((*server_addr).sin_addr), ntohs(server_addr->sin_port));
        goto err_over;
    }
    // LOG_INFO(sock_fd, "connect to %s:%d", ip, port);
    return sock_fd;

    err_over:
    shutdown(sock_fd, SHUT_RDWR);
    return -1;
}

/* 处理僵尸进程 */
void sigchld_handler(int signal) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int recv_data(int fd, char *buf, int buf_len, enum BOL isde) {
    int n = recv(fd, buf, buf_len, 0);
    // printf("---------recv------------\n%s\n", buf);
    if (TRUE == isde) {
        // printf("---------recv------------\n");
        // print_num(buf, buf_len);
        encode_map(buf, buf_len, DECRYPT);
        // print_num(buf, buf_len);
    }
    return n;
}

int send_data(int fd, char *buf, int buf_len, enum BOL isen) {
    if (TRUE == isen) {
        // printf("---------send %d %s------------\n", buf_len, buf);
        // print_num(buf, buf_len);
        encode_map(buf, buf_len, ENCRYPT);
        // print_num(buf, buf_len);
    }
    return send(fd, buf, buf_len, 0);
}

int fork_forward(int src_sock, int target_sock, enum CryptMode mode) {
    int fk;
    if ((fk = fork()) != 0) { // 创建子进程用于从客户端转发数据到远端socket接口
        // LOG_ERR("fork fail [%d] \n", mode);
        return fk;
    }
    char buf[BUF_SIZE];
    int n;
    enum BOL if_recv_de = FALSE, if_send_en = FALSE;
    switch (mode) {
        case ENCRYPT: if_send_en = TRUE; break;
        case DECRYPT: if_recv_de = TRUE; break;
        default: break;
    }

    while ((n = recv_data(src_sock, buf, BUF_SIZE, if_recv_de)) > 0) {
        send_data(target_sock, buf, n, if_send_en);
    }
    shutdown(target_sock, SHUT_RDWR); 
    shutdown(src_sock, SHUT_RDWR); 
    exit(0);
}


int strToInt(const char* str, int start, int end) {
    int ret = 0;
    int ratio = 1;
    for (; end >= start; end--)
    {
        ret += (str[end] - '0') * ratio;
        ratio *= 10;
    }
    return ret;
}

bool is_ipv4(const char* str) {
    size_t len = strlen(str);
    int part_start_idx = 0, p_num = 0, i, num;
    if (str[0] == '0')
        return false;
    for (i = 0; i < len; i++) {
        if (str[i] == '\0')
            break;
        if (str[i] == '.') // 是 .
        {
            if (p_num == 3 || strToInt(str, part_start_idx, i-1) > 255)
                return false;
            p_num++;
            part_start_idx = i+1;
        }
        else if (str[i] < 48 || str[i] > 57)
            return false;
    }
    return true;
}

bool get_sockaddr_in(struct sockaddr_in *server_addr, const char *hostOrIp, const char *port) {
    struct hostent *server;
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(atoi(port));
    if (is_ipv4(hostOrIp)) {
        server_addr->sin_addr.s_addr = inet_addr(hostOrIp);
        return true;
    }
    if(NULL == (server = gethostbyname(hostOrIp))) {
        return false;
    }
    memcpy(&server_addr->sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    return true;
}