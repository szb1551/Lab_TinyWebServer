#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

class util_timer;

// 用户数据
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// 计时器双向链表
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire; // 时间，处理用户信息的时间

    void (*cb_func)(client_data *); // 一个函数指针
    client_data *user_data; //用户数据
    util_timer *prev;
    util_timer *next;
};

// 计时器的升序双向链表
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);    // 添加计时器，加到适合的位置
    void adjust_timer(util_timer *timer); // 调整双链表计时器，保持升序排列
    void del_timer(util_timer *timer);    // 删除计时器
    void tick();                          // 删除超时的节点

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

// 注册事件，网络编程连接，那一套东西的设置
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;       // 管道描述符
    sort_timer_lst m_timer_lst; // 升序双链表
    static int u_epollfd;       // epoll描述符
    int m_TIMESLOT;             // 最小时间片段？
};

// 关闭用户连接
void cb_func(client_data *user_data);

#endif
