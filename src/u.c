#include <stdio.h>
#include <stdlib.h> // atoi exit
#include <sys/socket.h>
#include <asm/socket.h>
#include <netinet/in.h> // sockaddr_in sizeof
#include <arpa/inet.h> // htons inet_addr
#include <errno.h>
// #include <sys/wait.h>
#include <pthread.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h> // fork close
#include <fcntl.h>

#ifdef __APPLE__

#else
#include <wait.h>
#endif

#define BOL_TRUE 1
#define BOL_FALSE 0
#define BUF_SIZE 8192 // 16*n
#define LOG_ERR(fmt...)  do { fprintf(stderr,"[%s %s - %s] ",__DATE__,__TIME__, strerror(errno)); fprintf(stderr, ##fmt); } while(0)
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

void encode_map(char *buf, int buf_len, char *key) {
    if (NULL == key) return;
    int i;
    // printf("%s(%d vs %d): mode=%d\n", buf, buf_len, i, mode);
    for (i = 0; i < buf_len; i++) {
        buf[i] = key[(unsigned char) buf[i]];
    }
    // printf("%s(%d vs %d): end mode=%d\n", buf, buf_len, i, mode);
}

int config_non_block(int fd){
    return fcntl(fd, F_SETFL, O_RDWR|O_NONBLOCK);   
}

/**
 * @param block TRUE:阻塞 FALSE:非阻塞
 */
int create_server_socket(int port, enum BOL block) {
    int sock_fd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
     //创建套接字
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) goto err_return;
    //将套接字设置为允许重复使用本机地址或者为设置为端口复用
    int on = 1;
    // SO_REUSEPORT SO_REUSEADDR
    if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0) goto err_over;    
    // 设置非阻塞
    if (FALSE == block && 0 != config_non_block(sock_fd)) goto err_over;
    //填充服务器网络信息结构体
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; //监听所有ip，值为0 不需要转换字节序
    server_addr.sin_port = htons(port);
    //将套接字与服务器网络信息结构体绑定
    if(bind(sock_fd, (struct sockaddr *)&server_addr, addr_len) < 0) goto err_over;
    //将套接字设置为被动监听状态
    if(listen(sock_fd, 5) < 0) goto err_over;
    return sock_fd;

    err_over:
    shutdown(sock_fd, SHUT_RDWR);
    err_return:
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
    if (TRUE == isde) {
        // printf("---------recv------------\n");
        // print_num(buf, buf_len);
        encode_map(buf, buf_len, key_de);
        // print_num(buf, buf_len);
    }
    return n;
}

int send_data(int fd, char *buf, int buf_len, enum BOL isen) {
    if (TRUE == isen) {
        // printf("---------send %d %s------------\n", buf_len, buf);
        // print_num(buf, buf_len);
        encode_map(buf, buf_len, key_en);
        // print_num(buf, buf_len);
    }
    return send(fd, buf, buf_len, 0);
}