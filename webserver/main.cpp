#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include "locker.h"
#include "threadpool.h"
#include "http_connection.h"

#define MAX_FD 65536           // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大的事件数量

extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);

void addSignal(int sig, void(handler)(int))
{
    struct sigaction signal_action;
    memset(&signal_action, '\0', sizeof(signal_action));
    signal_action.sa_handler = handler;
    sigfillset(&signal_action.sa_mask);
    assert(sigaction(sig, &signal_action, NULL) != -1);
}

int main(int argc, char *argv[])
{

    if (argc <= 1)
    {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);
    addSignal(SIGPIPE, SIG_IGN);

    ThreadPool<HttpConnection> *pool = NULL;
    try
    {
        pool = new ThreadPool<HttpConnection>;
    }
    catch (...)
    {
        return 1;
    }

    HttpConnection *users = new HttpConnection[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    ret = listen(listenfd, 5);

    // 创建epoll对象，和事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    // 添加到epoll对象中
    addfd(epollfd, listenfd, false);
    HttpConnection::epollfd_ = epollfd;

    while (true)
    {

        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);

                if (connfd < 0)
                {
                    printf("errno is: %d\n", errno);
                    continue;
                }

                if (HttpConnection::user_count_ >= MAX_FD)
                {
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                users[sockfd].closeConnection();
            }
            else if (events[i].events & EPOLLIN)
            {
                if (users[sockfd].read())
                {
                    pool->addTask(users + sockfd);
                }
                else
                {
                    users[sockfd].closeConnection();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                if (!users[sockfd].write())
                {
                    users[sockfd].closeConnection();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);

    delete[] users;
    delete pool;

    return 0;
}