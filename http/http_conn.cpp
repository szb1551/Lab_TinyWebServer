#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <experimental/filesystem>

// 定义http响应的一些状态信息

namespace ff = experimental::filesystem;

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool)
{
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

char http_conn::Char2Int(char ch)
{
    if (ch >= '0' && ch <= '9')
        return (char)(ch - '0');
    if (ch >= 'a' && ch <= 'f')
        return (char)(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F')
        return (char)(ch - 'A' + 10);
    return -1;
}

char http_conn::Str2Bin(char *str)
{
    char tempWord[2];
    char chn;
    tempWord[0] = Char2Int(str[0]);         // make the B to 11 -- 00001011
    tempWord[1] = Char2Int(str[1]);         // make the 0 to 0  -- 00000000
    chn = (tempWord[0] << 4) | tempWord[1]; // to change the BO to 10110000
    return chn;
}

string http_conn::UrlDecode(string str)
{
    string output = "";
    char tmp[2];
    int i = 0, idx = 0, ndx, len = str.length();

    while (i < len)
    {
        if (str[i] == '%')
        {
            tmp[0] = str[i + 1];
            tmp[1] = str[i + 2];
            output += Str2Bin(tmp);
            i = i + 3;
        }
        else if (str[i] == '+')
        {
            output += ' ';
            i++;
        }
        else
        {
            output += str[i];
            i++;
        }
    }
    return output;
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str()); // 拷贝到sql_user
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    // LT读取数据
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    // ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t"); // 从string中找到以"\ t"为起点的内容并返回
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0) // 字符串比较，s1>s2返回大于0,小于则返回小于0的数
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t"); // 检索s1中第一个不再s2中出现的字符的下标
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7; // 指针增加7个字节
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/'); // 从s1中查找字符/
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "login.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 设置进程连接的状态信息
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    // LOG_INFO("所有信息:%s", m_read_buf);
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            LOG_INFO("CHECK STATE:%s", text);
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            LOG_INFO("CHECK HEADER:%s", text);
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT: // 检查post表单信息
        {
            LOG_INFO("message:%s", text);
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 响应请求内容
http_conn::HTTP_CODE http_conn::do_request()
{
    LOG_INFO("响应登陆:%s", m_url);
    strcpy(m_real_file, doc_root);
    LOG_INFO("拼接前文件%s", m_real_file);
    int len = strlen(doc_root);
    if(strchr(m_url, '?')) //如果带有参数的情况
    {
        char *result = strchr(m_url, '?');
        *result = '\0';
        LOG_INFO("处理完参数连接%s", m_url);
    }
    // printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/'); // m_url中最后一次出现'/'的位置
    // const char *p = strchr(m_url, '/'); // m_url中第一次出现'/'的位置

    // 处理cgi
    if (cgi == 1 && (string(p + 1) == "login.cgi" || string(p + 1) == "regist.cgi"))
    {
        // 0为新用户，1为已有账号，2为登陆标志，3为注册标志
        //  根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        // char *m_url_real = (char *)malloc(sizeof(char) * 200);
        // strcpy(m_url_real, "/");
        // strcat(m_url_real, m_url + 2);
        // strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        // free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        if (string(p + 1) == "regist.cgi")
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert); // 数据库查询指令
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/login.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (string(p + 1) == "login.cgi")
        {
            if (users.find(name) != users.end() && users[name] == password)
            {
                strcpy(m_url, "/views/index.html");
                int ans = 0; // 总的视频数量
                string video_path1 = m_real_file + string("/data/lxd_data/Videos/");
                string pdf_path1 = m_real_file + string("/data/lxd_data/Books/");
                vector<string> video_paths;
                vector<string> pdf_paths;
                vector<string> video_names;
                vector<string> pdf_names;
                video_paths.push_back(video_path1);
                pdf_paths.push_back(pdf_path1);
                for (const auto &path : video_paths)
                    for (const auto &entry : ff::directory_iterator(path))
                    {
                        if (ff::is_regular_file(entry))
                        {
                            video_names.push_back(entry.path());
                            ans++;
                        }
                    }
                for (const auto &path : pdf_paths)
                    for (const auto &entry : ff::recursive_directory_iterator(path))
                    {
                        if (ff::is_regular_file(entry))
                        {
                            string last = entry.path().extension().string();
                            if (last != string(".mp4"))
                            {
                                pdf_names.push_back(entry.path());
                            }
                            else
                            {
                                video_names.push_back(entry.path());
                                ans++;
                            }
                        }
                    }
                string video_save_path = m_real_file + string("/TinyWebServer/root/video_array.txt");
                string pdf_save_path = m_real_file + string("/TinyWebServer/root/pdf_array.txt");
                ifstream infile(video_save_path);
                ifstream infile2(pdf_save_path);
                if (!infile.good())
                {
                    locker m_mutex;
                    m_mutex.lock();
                    ofstream outfile(video_save_path);
                    for (int i = 0; i < video_names.size(); i++)
                    {
                        outfile << video_names[i];
                        if (i < video_names.size() - 1)
                            outfile << "\n";
                    }
                    m_mutex.unlock();
                }
                if (!infile2.good())
                {
                    locker m_mutex;
                    m_mutex.lock();
                    ofstream outfile(pdf_save_path);
                    for (int i = 0; i < pdf_names.size(); i++)
                    {
                        outfile << pdf_names[i];
                        if (i < pdf_names.size() - 1)
                            outfile << "\n";
                    }
                    m_mutex.unlock();
                }
            }
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (string(p + 1) == "regist")
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (string(p + 1) == "login")
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/login.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == 'T')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/views/SeeTeach.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == 'O')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/views/SeeOther.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == 'P')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/views/SeePdf.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == 'p' && *(p + 2) == '\0') // 访问pdf文件， 防止和pdf.png文件冲突
    {
        string temp(m_url);
        string output = UrlDecode(temp);
        strncpy(m_real_file + len, output.c_str(), output.size() - strlen(p));
        // LOG_INFO("实际访问连接%s", m_real_file);
    }
    else if (*(p + 1) == 'M') // 大写负责页面跳转，小写负责写入内容
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/views/SeeMd.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == 'm'&&*(p+2)=='\0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/data/lxd_data/容器管理");
        strcat(m_url_real, m_url);
        //strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real)-strlen(p));
        free(m_url_real);
    }
    else if (*(p + 1) == 'V')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/TinyWebServer/root/views/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == 'v' && *(p + 2) == '\0') // 防止和views冲突
    {
        string temp(m_url);
        string output = UrlDecode(temp);
        strncpy(m_real_file + len, output.c_str(), output.size() - strlen(p));
    }
    else
    {
        char root[] = "/TinyWebServer/root";
        strcat(m_real_file, root);
        len = strlen(m_real_file);
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    // stat函数，获取文件信息，成功返回0,失败-1
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    // 文件权限信息
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 文件属性信息，对应的模式
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    // LOG_INFO("实际访问文件%s", m_real_file);
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    return FILE_REQUEST;
}
// 删除地址信息
void http_conn::unmap()
{
    if (m_file_address)
    {
        // 删除特定地址区域的对象映射 (开始地址 映射长度 )
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
// 写入函数
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

// 添加响应 往写入缓冲池中加入参数
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
// 添加状态响应
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
// 添加header
bool http_conn::add_headers(int content_len)
{
    return add_accept_ranges() && add_content_length(content_len) && add_content_range(content_len) && add_linger() &&
           add_blank_line();
}
// 添加响应标签
bool http_conn::add_accept_ranges()
{
    return add_response("Accept-Ranges:%s\r\n", "bytes");
}
// 添加内容长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
// 添加内容范围
bool http_conn::add_content_range(int content_len)
{
    return add_response("Content-Range:bytes %d-%d/%d\r\n", 0, content_len, content_len);
}
// 添加内容类型
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 添加连接内容
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
// 添加空格
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
// 添加内容响应
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
// 进程写入信息，只有200正常访问状态才可以 返回真
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
// 处理用户数据的进程
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
