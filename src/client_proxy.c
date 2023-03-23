#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>
#include <netdb.h>
#include <resolv.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>

#include "common.c"

#if defined(OS_ANDROID)
#include <android/log.h>
 
#define LOG(fmt...) __android_log_print(ANDROID_LOG_DEBUG,__FILE__,##fmt)
 
#else
#define LOG(fmt...)  do { fprintf(stderr,"[%s %s - %s] ",__DATE__,__TIME__, strerror(errno)); fprintf(stderr, ##fmt); } while(0)
#endif


#define IS_HTTP 1
#define IS_HTTPS 2
int analyse_http(const char* buf, long buf_len, char *host, char *port);

// static int m_pid; /* 保存主进程id */

int main(int argc, char *argv[])
{
    if (argc < 5){
        printf("Usage: %s <locap_port> <remote_ip> <remopt_port> <secret>\n", argv[0]);
        exit(0);
    }

    key_init(argv[4]);

    int local_port = atoi(argv[1]);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[2]);
    server_addr.sin_port = htons(atoi(argv[3]));
    
    signal(SIGCHLD, sigchld_handler); // 防止子进程变成僵尸进程

    int server_sock, client_sock;
    if (-1 == (server_sock = create_server_socket(local_port))) {
        exit(0);
    }

    printf("open client port: %d \n", local_port);

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);
        if (fork() != 0) { // 创建子进程处理客户端连接请求
            close(client_sock);
            continue;
        }
        close(server_sock);

        char buf[BUF_SIZE];
        char remote_host[128];
        char remote_port[5];
        memset(buf, 0, BUF_SIZE);
        memset(remote_host, 0, 128);
        memset(remote_port, 0, 5);

        int read_len = recv(client_sock, buf, BUF_SIZE, 0);
        if (0 == read_len) {
            goto g_over;
        } else if (read_len < 0) {
            LOG_ERR("read_len err2: %d \n", read_len);
            goto g_over;
        }
        
        int is_http_tunnel = analyse_http(buf, read_len, remote_host, remote_port);
        strcat(remote_host, ":");
        strcat(remote_host, remote_port);
        strcat(remote_host, "\r\n");
        // printf("--- to s(%ld): %s---\n", strlen(remote_host), remote_host);

        int remote_sock;
        if ((remote_sock = create_connect_socket(&server_addr)) < 0) {
            LOG("Cannot connect to host [%s:%s] \n", argv[2], argv[3]);
            break;
        }

        send_data(remote_sock, remote_host, strlen(remote_host), TRUE);
        memset(remote_port, 0, 5);
        int n = recv_data(remote_sock, remote_port, 5, TRUE);
        if (n <= 0) {
            printf("connect remote fail, len: %d\n", n);
            goto g_over2;
        }

        if ('2' != remote_port[0] || '0' != remote_port[1] || '0' != remote_port[2]) {
            printf("connect remote fail, code: %s\n", remote_port);
            goto g_over2;
        }

        if(IS_HTTPS == is_http_tunnel) {
            char * resp = "HTTP/1.1 200 Connection Established\r\n\r\n";
            if(send(client_sock, resp, strlen(resp), 0) < 0) {
                perror("Send http tunnel response  failed\n");
                goto g_over2;
            }
        } else if(IS_HTTP == is_http_tunnel) {
            send_data(remote_sock, buf, read_len, TRUE);
        }

        // browser -> server_proxy
        fork_forward(client_sock, remote_sock, ENCRYPT);
        // server_proxy -> browser
        fork_forward(remote_sock, client_sock, DECRYPT);

        g_over2:
        close(remote_sock);
        g_over:
        close(client_sock);
        exit(0);
    }
    return 0;

}

/**
 * @brief 解析http请求
 * 
 * @param buf 
 * @return 0:不是http 1:是http
 */
int analyse_http(const char* buf, long buf_len, char *host, char *port) {
    int idx = 0, i= 0;
    // https protocol
    if (0 == strncmp("CONNECT", buf, 7))
    {
        i = 8;
        for (; i < buf_len; i++) // host
        {
            if (':' == buf[i] || ' ' == buf[i])
            {
                break;
            }
            host[idx++] = buf[i];
        }
        if(':' == buf[i]) // port
        {
            idx = 0;
            i++; // skip ':'
            for (; i < buf_len; i++)
            {
                if (' ' == buf[i])
                {
                    break;
                }
                port[idx++] = buf[i];
            }
        }
        if (0 == strlen(port))
        {
            strcpy(port, "443");
        }
        return IS_HTTPS;
    }
    else 
    {
        // http protocol
        for (; i < buf_len; i++)
        {
            if (IS_HTTP_CRLF(buf, i) && 'H' == buf[i+2] && 'o' == buf[i+3] && 's' == buf[i+4] && 't' == buf[i+5] && ':' == buf[i+6])
            {
                idx = 0;
                i += 8; // skip "\r\nHost: "
                for(; i < buf_len; i++) // host
                {
                    if (':' == buf[i] || IS_HTTP_CRLF(buf, i))
                    {
                        break;
                    }
                    host[idx++] = buf[i];
                }
                if(':' == buf[i])
                {
                    idx = 0;
                    i++; // skip ':'
                    for(; i < buf_len; i++) // port
                    {
                        if (IS_HTTP_CRLF(buf, i))
                        {
                            break;
                        }
                        port[idx++] = buf[i];
                    }
                }
                if (0 == strlen(port))
                {
                    strcpy(port, "80");
                }
                return IS_HTTP;
            }
        }
    }
    return 0;
}
