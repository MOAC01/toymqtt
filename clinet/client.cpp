#include "../protocol.h"
#include "client.h"
#include <map>
#include <vector>
#include <set>
#include <regex>
#include <sstream>
#include <random>
#define KEEP_ALVIE 60

std::set<uint16_t> *messages;
// pthread_mutex_t mutex_messaes;

const char *help = "Usage: toymqtt-client [OPTION]... to connect to broker\n\      
                        -b, --broker addr(required)\n\
                        -p, --broker port(required)\n\ 
                        -c, --client id(required)\n\
                        -C, --with no clean session\n\
                        -k, --keep alive\n\
                        -u, --user name\n\
                        -P, --password\n\
                        Examples:\n\
                       toymqtt-client -b 127.0.0.1 -p 1833 -c test -C -k 60 -u username -p password\n\
                       toymqtt-client -b 127.0.0.1 -p 1833 -c test\n";

/**
 * PING 请求线程函数
 */
void *ping_request(void *arg)
{

    Ping *ping = (Ping *)arg;
    uint16_t time = ping->keep_alive;
    u_char ping_data[2];
    ping_data[0] = PINGREQ;
    ping_data[1] = 0; // 剩余长度为0

    while (true)
    {
        sleep(time);
        send(ping->fd, ping_data, 2, 0);
    }

    return nullptr;
}

/**
 * 接收Broker消息线程函数
 */
void *recv_proc(void *arg)
{
    u_char recv_data[1024];
    ssize_t recv_bytes = 0;
    int fd = *(int *)arg;
    void handle_publish(u_char * raw, ssize_t bytes, int);
    void handle_pubrel(u_char * raw, ssize_t bytes, int);
    void handle_pubcomp(u_char * raw, ssize_t bytes, int);

    while (true)
    {

        recv_bytes = recv(fd, recv_data, 1024, 0);

        if (recv_bytes <= 0)
        {
            perror("recv error");
            exit(EXIT_FAILURE);
        }

        u_char type = PUB_CHECK(recv_data[0]);

        switch (type)
        {
        case PUBLISH:
            handle_publish(recv_data, recv_bytes, fd);
            break;

        case PUBREC:
            // 回复PUBREL
            break;

        case PUBREL:
            // 回复PUBCOMP
            handle_pubrel(recv_data, recv_bytes, fd);
            break;

        case PUBCOMP:
            // 删除缓存的消息ID
            handle_pubcomp(recv_data, recv_bytes, fd);
            break;

        default:
            break;
        }

        memset(recv_data, 0, recv_bytes);
    }

    return nullptr;
}

void handle_pubrel(u_char *raw, ssize_t bytes, int fd)
{

    uint16_t msg_id = raw[2] << 8;
    msg_id += raw[3];
    if (messages->find(msg_id) != messages->end())
    {
        u_char resp[4];
        resp[0] = PUBCOMP;
        resp[1] = 2;              // 剩余长度
        memcpy(resp, raw + 2, 2); // 消息ID
        send(fd, resp, 4, 0);
        // pthread_mutex_lock(&mutex_messaes);
        messages->erase(msg_id);
        // pthread_mutex_unlock(&mutex_messaes);
    }
}

void handle_pubrec(u_char *raw, ssize_t bytes, int fd)
{
    uint16_t msg_id = raw[2] << 8;
    msg_id += raw[3];
    // pthread_mutex_lock(&mutex_messaes);
    messages->insert(msg_id);
    // pthread_mutex_unlock(&mutex_messaes);
    u_char resp[4];
    resp[0] = PUBREL;
    resp[1] = 2;              // 剩余长度
    memcpy(resp, raw + 2, 2); // 消息ID
}

void handle_pubcomp(u_char *raw, ssize_t bytes, int fd)
{
    uint16_t msg_id = raw[2] << 8;
    msg_id += raw[3];
    // pthread_mutex_lock(&mutex_messaes);
    messages->erase(msg_id);
    // pthread_mutex_unlock(&mutex_messaes);
}

void handle_publish(u_char *raw, ssize_t bytes, int fd)
{

    u_char data = raw[0] & 0xf; // 控制报文首字节高4位清零
    u_char retain = data & 0x01;
    u_char dup = (data & 0x08) >> 3; // 先把其它无关位清零再移到第0位置
    u_char qos = (data & 0x06) >> 1;
    uint16_t coneten_length;

    if (!dup)
    {
        uint16_t topic_len = raw[2] << 8;
        topic_len += (uint16_t)raw[3];
        char *topic = new char[topic_len + 1];
        // 获取发布的主题，发布主题在原报文偏移的第4字节开始，取topic_len个字节
        memcpy(topic, raw + 4, topic_len);
        topic[topic_len] = '\0';

        // payload 长度= 总长度-4字节固定头-主题长度-消息ID所占的2字节
        coneten_length = bytes - topic_len - 4 - 2;
        char *content = new char[coneten_length + 1];
        int index = 4 + topic_len;
        uint16_t message_ident = raw[index] << 8;
        message_ident += raw[index + 1];

        memcpy(content, raw + index + 2, coneten_length);
        content[coneten_length] = '\0';
        std::cout << "recv: " << content << "  topic: " << topic << "  QoS: " << (int)qos << std::endl;

        if (qos == 1)
        {
            char resp[4]; // qos1需要恢复PUBACK，共4字节
            resp[0] = PUBACK;
            resp[1] = 2;              // 剩余长度,消息id共占2字节
            memcpy(resp, raw + 8, 2); // 消息ID
            send(fd, resp, 4, 0);
        }

        else if (qos == 2)
        {

            // 收到PUBLIS, 回复PUBREC
            char resp[4]; // qos1需要恢复PUBACK，共4字节
            resp[0] = PUBREC;
            memcpy(resp, raw + 8, 2); // 消息ID
            send(fd, resp, 4, 0);
            // pthread_mutex_lock(&mutex_messaes);
            messages->insert(message_ident); // 保存消息ID
            // pthread_mutex_unlock(&mutex_messaes);
            // 收到PUBREL/,回复PUBCOM
        }
    }
}

bool is_toggle(const char *argv)
{
    return argv[0] == '-';
}

void copy_swap(void *dst, uint16_t *src)
{

    uint16_t *tmp = src;
    uint16_t backup = *tmp;
    uint16_t num = *tmp & 0x00ff; // 取原变量低字节
    num <<= 8;                    // 低字节移到新变量的高字
    *tmp &= 0xff00;               // 原变量低字节清零
    *tmp >>= 8;                   // 将原变量高字节移到低字节
    num |= *tmp;                  // 完成交换

    memcpy(dst, &num, 2);
    memcpy(src, &backup, 2); // 还原原变量的值
}

u_char *get_conncet_package(std::map<std::string, std::string> &optionsmap, size_t &bytes)
{

    // 固定报头包含1字节控制报文+1字节剩余长度+1字节协议名称长度+4字节协议名称(MQTT)+1字节
    size_t fix_header_size = 11;
    size_t varliable_size = 0;
    u_char *data = new u_char[1024];
    u_char conn_flag = 2;
    uint16_t client_id_size = 0;
    memset(data, 0, 1024);

    if (optionsmap.find("-k") != optionsmap.end())
    {
        // 填充keep-alive
        uint16_t keep_alive = std::stoi(optionsmap.at("-k"));
        copy_swap(data + 10, &keep_alive);
    }
    else
    {
        data[11] = 0x3c; // 默认60秒
    }

    if (optionsmap.find("-c") != optionsmap.end())
    {
        // 填充client id长度和client id字段
        std::string client_id = optionsmap.at("-c");
        client_id_size = client_id.size();
        varliable_size += client_id_size;
        copy_swap(data + 12, &client_id_size);
        memcpy(data + 14, client_id.c_str(), client_id_size);
    }

    if (optionsmap.find("-u") != optionsmap.end() && optionsmap.find("-P") != optionsmap.end())
    {
        std::string username = optionsmap.at("-u");
        std::string password = optionsmap.at("-P");
        uint16_t username_size = username.size();
        uint16_t password_size = password.size();
        varliable_size += username_size;
        varliable_size += password_size;
        conn_flag &= 0xc2; // 设置用户密码标志位为1

        size_t start = 13 + client_id_size; // 用户名长度在客户端ID之后
        copy_swap(data + start, &username_size);
        memcpy(data + start + 2, username.c_str(), username_size);
        start += 2;
        start += username_size;
        copy_swap(data + start, &password_size);
        start += password_size;
        memcpy(data + start, password.c_str(), password_size);
    }

    data[9] = conn_flag;
    return data;
}

void mqtt_connect(int fd, std::map<std::string, std::string> &optionsmap)
{
    size_t bytes = 0;
    u_char *connect_data = get_conncet_package(optionsmap, bytes);
    u_char result[16];

    if (bytes != 0)
    {
        // 发送MQTT连接请求
        send(fd, connect_data, bytes, 0);
    }

    ssize_t nbytes = recv(fd, result, 16, 0);
    if (nbytes > 0 && result[0] == CONNACK)
    {
        std::cout << "connected to broker.\n";
        delete connect_data;
    }

    else
    {
        perror("fail to connect to broker");
        exit(EXIT_FAILURE);
    }
}

u_char *get_package(std::vector<std::string> &vec, size_t &nbtes, int type)
{
    u_char *pac;
    size_t fixedsize = 0;
    size_t remaining_length = 0;
    uint16_t topic_length = vec[1].size();
    u_char qos;
    int min = 0, max = 65535;
    uint16_t message_ident;                            // 随机生成2字节的消息ID
    std::random_device seed;                           // 硬件生成随机数种子
    std::ranlux48 engine(seed());                      // 利用种子生成随机数引擎
    std::uniform_int_distribution<> distrib(min, max); // 设置随机数范围，并为均匀分布
    message_ident = distrib(engine);
    switch (type)
    {
    case 0: // 发布报文封装
        // pub topic contetn qos
        fixedsize = 4;             // 发布报文固定头占4字节
        fixedsize += topic_length; // 再加上主题和消息的长度
        fixedsize += vec[2].size();
        qos = (u_char)std::stoi(vec[3]);

        remaining_length = 2 + topic_length; // 剩余长度=2字节主题长度+主题实际长度+内容长度
        remaining_length += vec[2].size();
        if (qos > 0)
        {
            remaining_length += 2; // qos > 0 需要填写消息ID
            fixedsize += 2;
            pac = new u_char[fixedsize];
            pac[0] = PUBLISH;
            qos <<= 1;
            pac[0] |= qos;
            pac[1] = (u_char)remaining_length;
            copy_swap(pac + 2, &topic_length);
            memcpy(pac + 4, vec[1].c_str(), topic_length);                     // 主题内容
            copy_swap(pac + 4 + topic_length, &message_ident);                 // 消息ID
            memcpy(pac + 4 + topic_length + 2, vec[2].c_str(), vec[2].size()); // 消息内容
        }
        else
        {
            pac = new u_char[fixedsize];
            pac[0] = PUBLISH;
            pac[1] = (u_char)remaining_length;
            copy_swap(pac + 2, &topic_length);
            memcpy(pac + 4, vec[1].c_str(), topic_length);
            memcpy(pac + 4 + topic_length, vec[2].c_str(), vec[2].size());
        }

        break;
    case 1: // 订阅报文封装
        // sub topic qos
        fixedsize = 6;
        remaining_length = 4 + topic_length + 1; // 剩余长度
        pac = new u_char[fixedsize + topic_length + 1];
        pac[0] = SUBSCRIBE;
        pac[1] = (u_char)remaining_length;
        copy_swap(pac + 2, &message_ident);
        copy_swap(pac + 4, &topic_length);
        memcpy(pac + 6, vec[1].c_str(), topic_length);
        qos = (u_char)std::stoi(vec[2]);
        pac[sizeof(pac) - 1] = (u_char)qos;

        break;
    case 2: // 取消订阅报文封装

        // unsub topic
        fixedsize = 4;
        fixedsize += topic_length;
        pac[0] = UNSUBSCRIBE;
        remaining_length = 2 + 2 + topic_length;
        pac[1] = (u_char)remaining_length;
        copy_swap(pac + 2, &message_ident);
        pac[3] = (u_char)topic_length;
        memcpy(pac + 4 + topic_length, vec[1].c_str(), topic_length);

        break;
    case 3: // 断开连接报文封装
        pac = new u_char[2];
        pac[0] = DISCONNECT;
        pac[1] = 0;

        break;

    default:
        break;
    }

    nbtes = sizeof(pac);
    return pac;
}

bool check_args(int argc, const char **argv, std::map<std::string, std::string> &optionsmap)
{
    std::regex ip4_pattern(R"(\d+\.\d+\.\d+\.\d+)");
    std::regex num_pattern(R"(\d+)");

    if (argc < 7)
    {
        puts(help);
        return false;
    }

    for (size_t i = 1; i < argc; i++)
    {

        if (strcmp(argv[i], "-C") == 0)
        {
            optionsmap["-C"] = "true";
        }
        else if (is_toggle(argv[i]) && i < argc - 1)
        {
            if (!is_toggle(argv[i + 1]))
            {
                optionsmap[argv[i]] = argv[i + 1];
            }
        }
    }
    
    

    return true;
}

int main(int argc, char const *argv[])
{

    std::map<std::string, std::string> optionsmap;

    bool check = check_args(argc, argv, optionsmap); // 命令行参数检查

    if (!check)
        exit(EXIT_FAILURE);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    if (lfd == -1)
    {
        perror("faild to create socket");
        exit(EXIT_FAILURE);
    }

    sock_addr_info sinfo;
    socklen_t slen = sizeof(sinfo);

    sinfo.sin_family = AF_INET;
    sinfo.sin_addr.s_addr = inet_addr(optionsmap.at("-b").c_str());
    sinfo.sin_port = htons(std::stoi(optionsmap.at("-p").c_str()));

    if (connect(lfd, (sock_addr *)&sinfo, slen) == -1)
    {
        // tcp三次握手
        perror("faild to connect to broker");
        close(lfd);
        exit(EXIT_FAILURE);
    }

    mqtt_connect(lfd, optionsmap); // mqtt连接请求
    // pthread_mutex_init(&mutex_messaes, nullptr);

    // 连接到broker成功后直接起一个线程用于ping请求，对应keep-alive功能，由于这个功能都是发送
    // 相同的数据，并且一直重复发送，也就是这个线程会一直执行直到客户端退出，所以这里我们就不用
    // 线程池了，直接创建线程
    pthread_t ping_thread;
    uint16_t keep_alive = KEEP_ALVIE;
    if (optionsmap.find("-k") != optionsmap.end())
    {
        keep_alive = std::stoi(optionsmap.at("-c"));
    }
    Ping ping{lfd, keep_alive};
    pthread_create(&ping_thread, nullptr, ping_request, (void *)&ping);
    pthread_detach(ping_thread);

    // 另起一个线程用来接收Broker的消息，由于也是一直运行，所以也不用线程池
    pthread_t recv_thread;
    pthread_create(&recv_thread, nullptr, recv_proc, &lfd);
    pthread_detach(recv_thread);

    // 主线程负责读取终端命令，向broker发送消息
    while (true)
    {
        char cmd[512];
        std::cin >> cmd;
        std::stringstream ss(cmd);
        std::string str;
        std::vector<std::string> cmdvec;
        size_t nbytes;
        u_char *data;
        while (getline(ss, str, ' '))
        {
            cmdvec.push_back(str);
        }

        if (cmdvec[0] == "pub")
        {
            data = get_package(cmdvec, nbytes, 0);
        }
        else if (cmdvec[0] == "sub")
        {
            data = get_package(cmdvec, nbytes, 1);
        }
        else if (cmdvec[0] == "unsub")
        {
            data = get_package(cmdvec, nbytes, 2);
        }
        else if (cmdvec[0] == "quit")
        {
            data = get_package(cmdvec, nbytes, 3);
        }
        else if (cmdvec[0] == "help")
        {
        }
        else
        {
            std::cout << "unkonwn command '" << cmdvec[0] << "'\n";
            continue;
        }
        if (nbytes > 0)
            send(lfd, data, nbytes, 0);
    }

    close(lfd);
    // pthread_mutex_destroy(&mutex_messaes);
    return 0;
}
