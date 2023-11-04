#include "http_connection.h"

// 定义HTTP响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char *doc_root = "/home/robin/webserver/resources";

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);

    return old_option;
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot) {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modifyfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 所有的客户数
int HttpConnection::user_count_ = 0;
// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int HttpConnection::epollfd_ = -1;

// 初始化连接,外部调用初始化套接字地址
void HttpConnection::init(int sockfd, const sockaddr_in &addr) {
    sockfd_ = sockfd;
    address_ = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(epollfd_, sockfd, true);
    user_count_++;
    init();
}

void HttpConnection::init() {
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;

    check_state_ = CHECK_STATE_REQUESTLINE;  // 初始状态为检查请求行
    is_link_ = false;                        // 默认不保持链接  Connection : keep-alive保持连接

    method_ = GET;  // 默认请求方式为GET
    url_ = 0;
    version_ = 0;
    content_length_ = 0;
    host_ = 0;
    start_line_ = 0;
    checked_index_ = 0;
    read_index_ = 0;
    write_index_ = 0;

    bzero(read_buffer_, READ_BUFFER_SIZE);
    bzero(write_buffer_, READ_BUFFER_SIZE);
    bzero(real_file_, FILENAME_LEN);
}

// 关闭连接
void HttpConnection::closeConnection() {
    if (sockfd_ != -1) {
        removefd(epollfd_, sockfd_);
        sockfd_ = -1;
        user_count_--;  // 关闭一个连接，将客户总数量-1
    }
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConnection::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = processRead();
    if (read_ret == NO_REQUEST) {
        modifyfd(epollfd_, sockfd_, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = processWrite(read_ret);
    if (!write_ret) {
        closeConnection();
    }
    modifyfd(epollfd_, sockfd_, EPOLLOUT);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool HttpConnection::read() {
    if (read_index_ >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        // 从read_buffer_ + read_index_索引出开始保存数据，大小是READ_BUFFER_SIZE - read_index_
        bytes_read = recv(sockfd_, read_buffer_ + read_index_, READ_BUFFER_SIZE - read_index_, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据
                break;
            }
            return false;
        } else if (bytes_read == 0) {  // 对方关闭连接
            return false;
        }
        read_index_ += bytes_read;
    }

    return true;
}

// 写HTTP响应
bool HttpConnection::write() {
    int temp = 0;

    if (bytes_to_send_ == 0) {
        // 将要发送的字节为0，这一次响应结束。
        modifyfd(epollfd_, sockfd_, EPOLLIN);
        init();
        return true;
    }

    while (1) {
        // 分散写
        temp = writev(sockfd_, io_vec_, io_vec_count_);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN) {
                modifyfd(epollfd_, sockfd_, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send_ += temp;
        bytes_to_send_ -= temp;

        if (bytes_have_send_ >= io_vec_[0].iov_len) {
            io_vec_[0].iov_len = 0;
            io_vec_[1].iov_base = file_address_ + (bytes_have_send_ - write_index_);
            io_vec_[1].iov_len = bytes_to_send_;
        } else {
            io_vec_[0].iov_base = write_buffer_ + bytes_have_send_;
            io_vec_[0].iov_len = io_vec_[0].iov_len - temp;
        }

        if (bytes_to_send_ <= 0) {
            // 没有数据要发送了
            unmap();
            modifyfd(epollfd_, sockfd_, EPOLLIN);

            if (is_link_) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

// 主状态机，解析请求
HttpConnection::HTTP_CODE HttpConnection::processRead() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while (((check_state_ == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) ||
           ((line_status = parseLine()) == LINE_OK)) {
        // 获取一行数据
        text = getLine();
        start_line_ = checked_index_;
        printf("got 1 http line: %s\n", text);

        switch (check_state_) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return doRequest();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST) {
                    return doRequest();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool HttpConnection::processWrite(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR:
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if (!addContent(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            addStatusLine(400, error_400_title);
            addHeaders(strlen(error_400_form));
            if (!addContent(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if (!addContent(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if (!addContent(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            addStatusLine(200, ok_200_title);
            addHeaders(file_state_.st_size);
            io_vec_[0].iov_base = write_buffer_;
            io_vec_[0].iov_len = write_index_;
            io_vec_[1].iov_base = file_address_;
            io_vec_[1].iov_len = file_state_.st_size;
            io_vec_count_ = 2;

            bytes_to_send_ = write_index_ + file_state_.st_size;

            return true;
        default:
            return false;
    }

    io_vec_[0].iov_base = write_buffer_;
    io_vec_[0].iov_len = write_index_;
    io_vec_count_ = 1;
    bytes_to_send_ = write_index_;

    return true;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
HttpConnection::HTTP_CODE HttpConnection::parseRequestLine(char *text) {
    // GET /index.html HTTP/1.1
    url_ = strpbrk(text, " \t");  // 判断第二个参数中的字符哪个在text中最先出现
    if (!url_) {
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *url_++ = '\0';  // 置位空字符，字符串结束符
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {  // 忽略大小写比较
        method_ = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    version_ = strpbrk(url_, " \t");
    if (!version_) {
        return BAD_REQUEST;
    }
    *version_++ = '\0';
    if (strcasecmp(version_, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
     */
    if (strncasecmp(url_, "http://", 7) == 0) {
        url_ += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        url_ = strchr(url_, '/');
    }
    if (!url_ || url_[0] != '/') {
        return BAD_REQUEST;
    }
    check_state_ = CHECK_STATE_HEADER;  // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
HttpConnection::HTTP_CODE HttpConnection::parseHeaders(char *text) {
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取content_length_字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (content_length_ != 0) {
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            is_link_ = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        content_length_ = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    } else {
        printf("oop! unknow header %s\n", text);
    }

    return NO_REQUEST;
}

// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
HttpConnection::HTTP_CODE HttpConnection::parseContent(char *text) {
    if (read_index_ >= (content_length_ + checked_index_)) {
        text[content_length_] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址file_address_处，并告诉调用者获取文件成功
HttpConnection::HTTP_CODE HttpConnection::doRequest() {
    // "/home/nowcoder/webserver/resources"
    strcpy(real_file_, doc_root);
    int len = strlen(doc_root);
    strncpy(real_file_ + len, url_, FILENAME_LEN - len - 1);

    // 获取real_file_文件的相关的状态信息，-1失败，0成功
    if (stat(real_file_, &file_state_) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(file_state_.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(file_state_.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(real_file_, O_RDONLY);
    // 创建内存映射
    file_address_ = (char *)mmap(0, file_state_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

// 解析一行，判断依据\r\n
HttpConnection::LINE_STATUS HttpConnection::parseLine() {
    char temp;
    for (; checked_index_ < read_index_; ++checked_index_) {
        temp = read_buffer_[checked_index_];
        if (temp == '\r') {
            if ((checked_index_ + 1) == read_index_) {
                return LINE_OPEN;
            } else if (read_buffer_[checked_index_ + 1] == '\n') {
                read_buffer_[checked_index_++] = '\0';
                read_buffer_[checked_index_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if ((checked_index_ > 1) && (read_buffer_[checked_index_ - 1] == '\r')) {
                read_buffer_[checked_index_ - 1] = '\0';
                read_buffer_[checked_index_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OK;
}

// 对内存映射区执行munmap操作
void HttpConnection::unmap() {
    if (file_address_) {
        munmap(file_address_, file_state_.st_size);
        file_address_ = 0;
    }
}

// 往写缓冲中写入待发送的数据
bool HttpConnection::addResponse(const char *format, ...) {
    if (write_index_ >= WRITE_BUFFER_SIZE) {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(write_buffer_ + write_index_, WRITE_BUFFER_SIZE - 1 - write_index_, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - write_index_)) {
        return false;
    }

    write_index_ += len;
    va_end(arg_list);

    return true;
}

bool HttpConnection::addContent(const char *content) { return addResponse("%s", content); }

bool HttpConnection::addContentType() { return addResponse("Content-Type:%s\r\n", "text/html"); }

bool HttpConnection::addStatusLine(int status, const char *title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConnection::addHeaders(int content_len) {
    addContentLength(content_len);
    addContentType();
    addIsLink();
    addBlankLine();
}

bool HttpConnection::addContentLength(int content_len) { return addResponse("Content-Length: %d\r\n", content_len); }

bool HttpConnection::addIsLink() {
    return addResponse("Connection: %s\r\n", (is_link_ == true) ? "keep-alive" : "close");
}

bool HttpConnection::addBlankLine() { return addResponse("%s", "\r\n"); }