#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdarg>
#include <cassert>
#include "locker.h"

class HttpConnection
{
public:
    static const int FILENAME_LEN = 200;       // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小

    // HTTP请求方法，这里只支持GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    //解析客户端请求时，主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, //当前正在分析请求行
        CHECK_STATE_HEADER,          //当前正在分析头部字段
        CHECK_STATE_CONTENT          //当前正在解析请求体
    };

    //服务器处理HTTP请求的可能结果，报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,        //请求不完整，需要继续读取客户数据
        GET_REQUEST,       //表示获得了一个完成的客户请求
        BAD_REQUEST,       //表示客户请求语法错误
        NO_RESOURCE,       //表示服务器没有资源
        FORBIDDEN_REQUEST, //表示客户对资源没有足够的访问权限
        FILE_REQUEST,      //文件请求,获取文件成功
        INTERNAL_ERROR,    //表示服务器内部错误
        CLOSED_CONNECTION  //表示客户端已经关闭连接了
    };

    // 从状态机的三种可能状态，即行的读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0, //读取到一个完整的行
        LINE_BAD,    //行出错
        LINE_OPEN    //行数据尚且不完整
    };

public:
    HttpConnection() {}
    ~HttpConnection() {}

public:
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接受的连接
    void closeConnection();                         // 关闭连接
    void process();                                 // 处理客户端请求
    bool read();                                    // 非阻塞读
    bool write();                                   // 非阻塞写

private:
    void init();                      // 初始化连接
    HTTP_CODE processRead();          // 解析HTTP请求
    bool processWrite(HTTP_CODE ret); // 填充HTTP应答

    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parseRequestLine(char *text);
    HTTP_CODE parseHeaders(char *text);
    HTTP_CODE parseContent(char *text);
    HTTP_CODE doRequest();
    char *getLine() { return read_buffer_ + start_line_; }
    LINE_STATUS parseLine();

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool addResponse(const char *format, ...);
    bool addContent(const char *content);
    bool addContentType();
    bool addStatusLine(int status, const char *title);
    bool addHeaders(int content_length);
    bool addContentLength(int content_length);
    bool addIsLink();
    bool addBlankLine();

public:
    static int epollfd_;    // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int user_count_; // 统计用户的数量

private:
    int sockfd_; // 该HTTP连接的socket和对方的socket地址
    sockaddr_in address_;

    char read_buffer_[READ_BUFFER_SIZE]; // 读缓冲区
    int read_index_;                     // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int checked_index_;                  // 当前正在分析的字符在读缓冲区中的位置
    int start_line_;                     // 当前正在解析的行的起始位置

    CHECK_STATE check_state_; // 主状态机当前所处的状态
    METHOD method_;           // 请求方法

    char real_file_[FILENAME_LEN]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char *url_;                    // 客户请求的目标文件的文件名
    char *version_;                // HTTP协议版本号，我们仅支持HTTP1.1
    char *host_;                   // 主机名
    int content_length_;           // HTTP请求的消息总长度
    bool is_link_;                 // HTTP请求是否要求保持连接

    char write_buffer_[WRITE_BUFFER_SIZE]; // 写缓冲区
    int write_index_;                      // 写缓冲区中待发送的字节数
    char *file_address_;                   // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat file_state_;               // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec io_vec_[2];               // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int io_vec_count_;

    int bytes_to_send_;   // 将要发送的数据的字节数
    int bytes_have_send_; // 已经发送的字节数
};

#endif
