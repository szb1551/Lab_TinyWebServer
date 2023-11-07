#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>
#include <iconv.h> //转中文字符

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag; // 是否需要处理定时器
    int improv;     // 是否读取完连接用户数据

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; }; // 返回已读池中的start开始的字符串信息
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_accept_ranges();
    bool add_content_length(int content_length);
    bool add_content_range(int content_len);
    bool add_linger();
    bool add_blank_line();
    char Char2Int(char ch);       // 字符转INT
    char Str2Bin(char *str);      // 字符转字
    string UrlDecode(string str); // 转中文

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state; // 读为0, 写为1

private:
    int m_sockfd;                      // 接受的用户操作描述符
    sockaddr_in m_address;             // 连接ip地址
    char m_read_buf[READ_BUFFER_SIZE]; // 读取缓冲区
    long m_read_idx;                   // 待读取的最大索引
    long m_checked_idx;                // 检查读取缓冲区的索引，
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写入的缓冲区
    int m_write_idx;                     // 缓冲区开始写入的位置
    CHECK_STATE m_check_state;           // 检查连接状态
    METHOD m_method;
    char m_real_file[FILENAME_LEN]; // 实际要打开的文件路径
    char *m_url;                    // 连接url
    char *m_version;                // http版本
    char *m_host;                   // 主机地址
    long m_content_length;          // 响应内容长度
    bool m_linger;                  // 连接方式？长连接？
    char *m_file_address;           // 文件地址信息
    struct stat m_file_stat;        // 文件
    struct iovec m_iv[2];           // 待写入/发送数据的位置和大小信息， 拼接两个数组
    int m_iv_count;                 // 待写入/发送数组长度
    int cgi;                        // 是否启用的POST
    char *m_string;                 // 存储请求头数据
    int bytes_to_send;              // 待发送字节长度
    int bytes_have_send;            // 已发送长度
    char *doc_root;                 // root文件夹路径

    map<string, string> m_users; // 用户字典，没有就加入
    int m_TRIGMode;              // 触发模式
    int m_close_log;             // 关闭日志

    char sql_user[100];   // 数据库用户名
    char sql_passwd[100]; // 数据库密码
    char sql_name[100];   // 数据库名
};

#endif
