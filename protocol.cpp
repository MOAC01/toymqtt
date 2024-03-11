#include "threadpool.h"
#include "protocol.h"
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <memory>
#include <fstream>
#include <iosfwd>

const int accept_conncet_key = 1;
const int handle_message_key = 0;
const int send_qos1_key = 2;
const int send_qos2_key = 3;
const std::string default_storage_path = "/usr/local/mqtt/data/";
std::map<std::string, std::map<int, Subscriber>> *client_map;
std::map<std::string, int> *save_session_map;
std::set<uint32_t> *messages;
ThreadPool *threadpool;

pthread_mutex_t mutex_map;     //
pthread_mutex_t mutex_session; // 锁存储clean_session的客户端的map

//默认配置
int qos1_wait_time = 60;
int qos2_wait_time = 60;
int max_thread_num = 20;
int min_thread_num = 1;
int task_queque_size = 10;

/**
 * 根据遗嘱消息类封装发布消息
 * 参数：will: 遗嘱消息实体; nbytes: 封装后的大小
 */
u_char *package_will_data(WillMessage *will, ssize_t &nbytes)
{
    void copy_swap(void *, void *);
    std::string topic = will->get_will_topic();
    ssize_t msg_len = will->get_payload_szie();
    uint16_t topic_len = topic.size();
    ssize_t size = 4 + msg_len + topic_len;
    u_char *data = new u_char[size];
    data[0] = PUBLISH;
    copy_swap(data + 2, &topic_len);
    int8_t remaining_length = (int8_t)(2 + topic_len + msg_len);
    data[1] = (char)remaining_length;                 // 剩余长度
    memcpy(data + 4, topic.c_str(), topic.size());    // 遗嘱主题
    will->get_payload(data + 4 + topic_len, msg_len); // 消息体
    nbytes = size;
    return data;
}

/*
 *
 * 交换一个int变量的高低字节(相当于汇编里的xchg指令)，将其赋给目标内存地址
 * 参数：
 * dst:目标内存地址
 * src:源操作数内存地址
 * Swap high-byte and low-byte of a integer(2 bytes) variable(similar to xchg in asm),
 * then copy it to destination memory
 * Parameter:
 * dst: target mem address
 * src: source variable mem address
 */
void copy_swap(void *dst, void *src)
{

    uint16_t *tmp = (uint16_t *)src;
    uint16_t backup = *tmp;
    uint16_t num = *tmp & 0x00ff; // 取原变量低字节
    num <<= 8;                    // 低字节移到新变量的高字
    *tmp &= 0xff00;               // 原变量低字节清零
    *tmp >>= 8;                   // 将原变量高字节移到低字节
    num |= *tmp;                  // 高字节为全0再和tmp与操作即完成交换

    memcpy(dst, &num, 2);
    memcpy(src, &backup, 2);      // 还原原变量的值
}

/**
 * 协议解析 (protocol pasering)
 * 参数(parameter)：
 * raw_data:   客户端请求报文(client requst data)
 * _nrawbytes: 报文长度 (request data length)
 * retbytes:   返回客户端报文的长度 (response data length)
 * set_keep_alive: 是否已设置过keep_alive (is seted up keep_alive)
 * fd:  与客户端连接的文件描述符 (client file desciber)
 *
 */
void protocol_paser(u_char *raw_data, ssize_t _nrawbytes, ssize_t *retbytes,
                    bool *set_keep_alive, WillMessage *will, int fd)
{
    char rlength = 0; // 剩余长度
    int do_publish(u_char *, ssize_t, int);
    int do_subscribe(u_char *, ssize_t, bool, int);
    sock_addr_info client;
    socklen_t clen = sizeof(client);
    int64_t keep_alive;
    uint16_t msg_ident;
    uint32_t client_uid;
    void init_connection(u_char *, int, bool *, WillMessage *);

    u_char type = PUB_CHECK(raw_data[0]);
    switch (type)
    {
    case CONNECT: // 客户端连接请求

        init_connection(raw_data, fd, set_keep_alive, will);
        memset(raw_data, 0, _nrawbytes); // 清空原始数据
        raw_data[0] = CONNACK;
        raw_data[1] = 2; // 剩余长度
        // 以下内容为剩余长度的内容
        raw_data[2] = 0;
        raw_data[3] = RETCODE;
        *retbytes = 4;
        break;

    case PUBLISH:

        // 所有Qos等级的第一阶段(the first stage of all the Qos level)
        *retbytes = do_publish(raw_data, _nrawbytes, fd);
        break;

    case DISCONNECT:
        *retbytes = 1;
        break;

    case SUBSCRIBE:
        *retbytes = do_subscribe(raw_data, _nrawbytes, false, fd);
        break;

    case UNSUBSCRIBE:
        *retbytes = do_subscribe(raw_data, _nrawbytes, true, fd);
        break;

    case PUBACK:

        // Qos1第二阶段(Broker作为发送方)
        getpeername(fd, (sock_addr *)&client, &clen);
        client_uid = client.sin_addr.s_addr + client.sin_port;
        msg_ident = raw_data[2] << 8;
        msg_ident += (uint16_t)raw_data[3];
        messages->insert(msg_ident + client_uid);
        break;

    case PUBREC:

        // Qos2第二阶段(Broker作为发送方), 缓存消息ID并回复PUBREL包
        getpeername(fd, (sock_addr *)&client, &clen);
        client_uid = client.sin_addr.s_addr + client.sin_port;
        msg_ident = raw_data[2] << 8;
        msg_ident += (uint16_t)raw_data[3];
        messages->insert(msg_ident + client_uid); // 保存消息ID
        memset(raw_data, 0, 4);
        raw_data[0] = PUBREL;
        raw_data[1] = 2;
        copy_swap(raw_data, &msg_ident);
        *retbytes = 4;

    case PUBREL:

        // Qos2第三阶段(Broker作为接收方)，删除阶段2缓存的消息id,并回复PUBCOMP包
        // Stage 3 of Qos2 (Broker is the reciver), remove the message id was storaged at
        // stage 2 and reply a PUBCOMP package
        getpeername(fd, (sock_addr *)&client, &clen);
        client_uid = client.sin_addr.s_addr + client.sin_port;
        msg_ident = raw_data[2] << 8;
        msg_ident += (uint16_t)raw_data[3];
        messages->erase(msg_ident + client_uid);
        raw_data[0] = PUBCOMP; // 回复PUBCOMP包
        raw_data[1] = 2;       // 剩余长度
        copy_swap(raw_data + 2, &msg_ident);
        *retbytes = 4;

    case PUBCOMP:
        // Qos2第三阶段(Broker作为发送方)，删除阶段2缓存的消息id,并回复PUBCOMP包
        getpeername(fd, (sock_addr *)&client, &clen);
        client_uid = client.sin_addr.s_addr + client.sin_port;
        msg_ident = raw_data[2] << 8;
        msg_ident += (uint16_t)raw_data[3];
        messages->erase(msg_ident + client_uid);
        break;

    case PINGREQ:
        memset(raw_data, 0, _nrawbytes);
        raw_data[0] = PINGRESP;
        raw_data[1] = rlength;
        *retbytes = 2;
        break;

    default:
        break;
    }
}

/**
 * 初始化客户端连接
 */
void init_connection(u_char *raw, int fd, bool *set_keep_alive, WillMessage *will_msg)
{
    sock_addr_info client;
    socklen_t clen = sizeof(client);
    struct timeval tv_alive_timeout;
    // 协议名长度位于报文2，3字节 (the protocol name length is located at 2nd, 3rd byte of raw message)
    int plen = raw[2] << 8;
    plen += (int)raw[3];
    int start = 4;              // 协议名起始(the start position of protocol name)
    int end = start + plen - 1; // 协议名结束(the end position of protocol name)
    // show_protocol(raw_data, s, e);       // 显示协议信息
    u_char flag = raw[end + 2]; // 连接标志位在协议名结束后的偏移2字节
    u_char clean_session = flag & 0x02;

    uint16_t client_id_len = raw[12] << 8;
    client_id_len += raw[13];
    if (clean_session == 0)
    {
        char *client_id = new char[client_id_len + 1];
        memcpy(client_id, raw + 14, client_id_len);
        client_id[client_id_len - 1] = '\0';
        std::string m_client_id(client_id);
        pthread_mutex_lock(&mutex_session);
        (*save_session_map)[m_client_id] = fd;
        pthread_mutex_unlock(&mutex_session);
    }

    int64_t keep_alive = raw[end + 3] << 8;
    keep_alive += (int64_t)raw[end + 4];

    if (!(*set_keep_alive))
    {
        // 设置会话超时时间，对应keep-alive标志,客户端心跳检测，如果客户端在设定的时间内没有发来任何消息，则会超时
        tv_alive_timeout.tv_sec = keep_alive;
        tv_alive_timeout.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv_alive_timeout, clen);
        *set_keep_alive = true;
    }

    // 遗嘱消息机制
    u_char will_flag = flag & 0x08;
    if (will_flag > 0)
    {
        // 遗嘱Qos
        u_char will_qos = flag & 0x18; // 保留两位qos,其他位清零
        will_qos >>= 3;                // 在原来的位置右移3位到第0，1位
        will_msg->set_will_qos(will_qos);

        // 遗嘱保留
        u_char will_retain = will_flag & 0x20;
        will_retain >>= 5;
        will_msg->set_will_retain(will_retain);

        int will_topic_length_start = 14 + client_id_len + 1;
        uint16_t will_topic_length = raw[will_topic_length_start] << 8;
        will_topic_length += raw[will_topic_length_start + 1];

        // 遗嘱主题
        char *will_topic = new char[will_topic_length + 1];

        // 遗嘱主题占2字节，从遗嘱主题长度的后面第二字节开始就是遗嘱主题内容的开始
        memcpy(will_topic, raw + (will_topic_length_start + 2), will_topic_length);
        will_topic[will_topic_length - 1] = '\0';
        std::string tpoic(will_topic);
        will_msg->set_will_topic(tpoic);

        // 遗嘱消息内容从遗嘱主题内容之后开始，故是遗嘱主题长度起始+遗嘱主题长度+1
        int will_msg_len_start = will_topic_length_start + will_topic_length + 1;
        uint16_t will_msg_len = raw[will_msg_len] << 8;
        will_msg_len += raw[will_msg_len + 1];
        char *will_message = new char[will_msg_len];
        memcpy(will_message, raw + (will_msg_len_start + 2), will_msg_len);
        will_msg->inited = true;
        will_msg->set_payload(will_message);
    }

    std::cout << "init done.\n";
}

/**
 * 订阅主题(Subsribing a topic)
 * 参数：raw:请求报文，lastbyte:请求报文长度，unsub:是否是取消订阅操作，fd: 客户端文件描述符
 * 思路：将订阅的客户端的文件描述符和主题存到map中，一个主题有多个客户端订阅，则一个主题名(key) 有多个文件描述符(values)
 * 当有新的发布消息时，就根据发布的主题取出该主题所有订阅的客户端(文件描述符)一一发送
 * 
 */
int do_subscribe(u_char *raw, ssize_t lastbyte, bool unsub, int fd)
{

    int retbytes;
    // 消息id，消息id位于第2、3位，共占2字节，故用16位数据存
    // 高字节先左移8位，空出低字节，再加上剩下的一个字节数据
    uint16_t message_id = raw[2] << 8;
    message_id += (uint16_t)raw[3];

    // 订阅的主题名的长度
    uint16_t topic_len = raw[4] << 8;
    topic_len += (uint16_t)raw[5];

    char *topic = new char[topic_len + 1];
    topic[topic_len] = '\0';
    memcpy(topic, raw + 6, topic_len);

    // 消息体的总长度=1字节报文类型+1字节剩余长度+2字节消息ID+2字节主题长度+主题名
    std::string source(topic);
    u_char qos = raw[lastbyte - 1];
    int q = (int)qos;
    Subscriber *sub = new Subscriber;
    sub->tfd = fd;
    sub->set_qos(q);

    memset(raw, 0, lastbyte); // 清空原始数据
    if (!unsub)               // 订阅操作
    {

        sock_addr_info client;
        socklen_t clen = sizeof(client);
        getpeername(fd, (sock_addr *)&client, &clen);

        sub->uid = client.sin_addr.s_addr + client.sin_port;
        sub->message_ident = message_id;
        std::cout << "msg type subscribe qos " << q << std::endl;

        if (client_map->find(source) != client_map->end())
        {
            // 如果需要订阅的主题已经存在，则把文件描述符加入该主题的map中
            // if the topic is already existsed, put the file descriptor into the map of the topic
            pthread_mutex_lock(&mutex_map);
            std::map<int, Subscriber> map = (*client_map)[source];
            map[fd] = *sub; //
            pthread_mutex_unlock(&mutex_map);
        }

        else
        {
            // 否则创建该主题，并加入map
            pthread_mutex_lock(&mutex_map);
            std::map<int, Subscriber> *subscriber_map = new std::map<int, Subscriber>;
            subscriber_map->insert(std::make_pair(fd, *sub));
            client_map->insert(std::make_pair(source, *subscriber_map));
            delete subscriber_map;
            pthread_mutex_unlock(&mutex_map);
            std::cout << "created topic: " << topic << ",added fd = " << fd << std::endl;
        }

        raw[0] = SUBACK;
        raw[1] = 3;   // 剩余长度
        raw[4] = qos; // 最后一个字节是客户端订阅的qos
        retbytes = 5; // 订阅ACK包，共5字节
    }
    else // 取消订阅(Unsubscribe)
    {
        std::cout << "msg type unsubscribe" << std::endl;
        if (client_map->find(source) != client_map->end())
        {
            pthread_mutex_lock(&mutex_map);
            std::map<int, Subscriber> map = (*client_map)[source];
            map.erase(fd);
            if (map.size() == 0)
            {
                // 如果某个主题的订阅量为0，删除此主题
                client_map->erase(source);
            }
            pthread_mutex_unlock(&mutex_map);
            std::cout << "removed fd = " << fd << std::endl;
        }

        raw[0] = UNSUBACK;
        raw[1] = 2; // 剩余长度
        retbytes = 4;
    }

    // 把消息id复制回原报文数组，如果直接用memcpy函数进行内存拷贝，原来消息id高低字节会相反，
    // 故需要先交换高低字节的值才能拷贝,不论是订阅还是取消订阅，第2，3字节都是消息ID
    copy_swap(raw + 2, &message_id);
    return retbytes;
}

/**
 * 将发送失败的消息写入磁盘, 仅当请求连接标志中的clean_session字段为0时有效
 *
 */
void failed(u_char *data, ssize_t nbytes, uint16_t msg_id, int fd, int qos)
{

    std::string client_id;

    for (auto &it : *save_session_map)
    {
        if (it.second == fd)
            client_id = it.first;
    }

    if (client_id.empty())
        return;

    // 文件名：messageid.dat
    std::string path = default_storage_path;
    path.append(client_id);
    path.append("/");

    if (access(path.c_str(), 0) == -1)
        mkdir(path.c_str(), S_IRWXU); // 先创建目录：path/client_id/

    path.append(std::to_string(msg_id));
    path.append(".dat"); // path/client_id/msg_id.dat

    std::ofstream out_file(path, std::ios::out | std::ios::binary);
    out_file.write((char *)data, nbytes); // 消息体写入磁盘
    out_file.close();

    SaveSessionInfo *session = new SaveSessionInfo; // 消息体的属性
    session->length = nbytes;
    session->fd = fd;
    session->target_qos = qos;
    std::string info_path = default_storage_path;
    info_path.append(client_id);
    info_path.append("/");
    info_path.append(std::to_string(msg_id));
    info_path.append("_info.dat"); // path/client_id/msg_id_info.dat
    std::ofstream out(info_path, std::ios::out | std::ios::binary);
    out.write((char *)session, sizeof(session)); // 写入磁盘
    out.close();
    delete session;
}

/**
 * 处理客户端发布者的主题和消息
 */
int do_publish(u_char *raw, ssize_t nbytes, int fd)
{

    void send_qos1(void *);
    void send_qos2(void *);
    int ret = 0;
    uint16_t ident;
    uint16_t retain_len = (uint16_t)raw[1]; // 剩余长度
    ssize_t coneten_length = 0;
    u_char data = raw[0] & 0xf; // 控制报文首字节高4位清零
    u_char retain = data & 0x01;
    u_char dup = (data & 0x08) >> 3; // 先把其它无关位清零再移到第0位置
    u_char qos = (data & 0x06) >> 1;

    int publisher_qos = (int)qos;

    // 主题长度占2字节，位于原报文偏移2，3字节
    uint16_t topic_len = raw[2] << 8;
    topic_len += (uint16_t)raw[3];
    char *topic = new char[topic_len + 1];

    int msgid_begin = 3 + topic_len + 1;

    if (qos > 0) // 如果QoS大于0，获取消息ID
    {
        ident = raw[msgid_begin] << 8;
        ident += (uint16_t)raw[msgid_begin + 1];
    }

    // 获取发布的主题，发布主题在原报文偏移的第4字节开始，取topic_len个字节
    memcpy(topic, raw + 4, topic_len);

    topic[topic_len] = '\0';
    std::string m_topic(topic);

    // payload的长度=总长度-(1字节的控制报文、标识1字节剩余长度、标识2字节主题长度、主题实际占的长度)
    coneten_length = nbytes - topic_len - 4;
    char *content = new char[coneten_length + 1];

    // 获取发布内容，内容位于报文的第4+主题之后
    memcpy(content, raw + topic_len + 4, coneten_length);
    content[coneten_length] = '\0';

    u_char *to_subscriber = new u_char[nbytes];
    memcpy(to_subscriber, raw, nbytes);
    memset(raw, 0, nbytes); // 清空原始数据(clear the raw data array)

    if (qos == 1)
    {
        // qos为1，需要回复发布者PUBACK包 (qos level is 1, should reply a PUBACK packet)
        raw[0] = PUBACK;
        raw[1] = 2; // 剩余长度，此时剩余的内容是2字节的发送方的消息ID
        copy_swap(raw + 2, &ident);
        ret = 4;
    }

    else if (qos == 2)
    {
        // Qos2第二阶段(Broker作为服务端接收)，缓存消息ID，回复PUBREC包
        // Stage 2 of Qos2 (Broker is the recvier), storage the message id, and reply a PUBREC package
        raw[0] = PUBREC;
        raw[1] = 2;
        copy_swap(raw + 2, &ident);
        ret = 4;

        sock_addr_info client;
        socklen_t clen = sizeof(client);
        getpeername(fd, (sock_addr *)&client, &clen);
        uint32_t cliet_uid = client.sin_addr.s_addr + client.sin_port + ident;

        if (messages->find(cliet_uid) != messages->end()) // 如果该消息已经接接收过，忽略
        {
            // std::cout << "message " << cliet_uid << " already recived!\n";
            return ret;
        }

        messages->insert(cliet_uid); // 把消息ID暂存
    }

    // 把发送者发布的消息体原样发给订阅此主题的订阅者
    // Send pubulisher's message body to every subscriber
    if (client_map->find(m_topic) != client_map->end())
    {
        std::map<int, Subscriber> map = (*client_map)[topic];
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            int subscriber_qos = (*it).second.get_qos();

            // 优先使用发布者的qos，如果订阅者的qos小于发布者的qos，则使用订阅者的qos
            int finally_qos = (subscriber_qos >= publisher_qos) ? publisher_qos : subscriber_qos;
            std::shared_ptr<SendQos> param = std::make_shared<SendQos>();
            param->fd = (*it).first;
            param->data = to_subscriber;
            param->bytes = nbytes;
            param->message_id = ident;
            param->client_uid = (*it).second.uid;

            // 因为qos大于0的情况涉及到等待ACK回复以及消息的重传(直到收到PUBACK包)等阻塞操作，
            // 所以需要放到另一个线程处理，不能阻塞当前线
            // Do not blocking currently thread, cause when qos gentler than 0, we'll wait for ACK reply and
            // resend the message until recieved the ACK
            if (finally_qos == 1)
            {
                to_subscriber[0] &= 0xf9;
                to_subscriber[0] |= 0x02;
                threadpool->put_task(send_qos1, &param, send_qos1_key);
            }
            else if (finally_qos == 2)
            {
                to_subscriber[0] &= 0xf9;
                to_subscriber[0] |= 0x04;
                threadpool->put_task(send_qos2, &param, send_qos2_key);
            }

            // 如果订阅主题时的qos为0则直接发送,不接收PUBACK
            else
            {
                to_subscriber[0] &= 0xf9;
                if (send((*it).first, to_subscriber, nbytes, 0) <= 0)
                    failed(to_subscriber, nbytes, ident, param->fd, 0); // 发送失败,客户端不在线
                // 当发送失败后就认为客户端已断开，这时应该检查客户端是否有设置clean_session，以便                                                     
                // 客户端再次连接时恢复此次会话
            }
        }
    }
    return ret;
}

/**
 * 通过qos1向订阅者发送发布者的消息，此函数放到线程池中执行
 *
 * 思路：发送操作由线程池中的线程执行，接收操作仍由先前接收发布者消息的线程执行，
 * 当接收线程收到了PUBACK包，把消息id写入一个公共的set, 当前线程发送后休眠一定的时间，
 * 然后检测set中是否已有该消息id，若有该消息id，说明接收线程已经接收到订阅者的PUBACK
 * 包，则从set中删除该消息id，退出循环，函数结束；若没有该消息id说明过了时间还是没有
 * 收到订阅者的PUBACK包，则进行重传，在重传之前将控制报文的DUP位设置为1，表明该消息是
 * 重复发送的
 *
 */
void send_qos1(void *arg)
{
    SendQos *param = (SendQos *)arg;
    u_char *raw = param->data;
    uint32_t key = param->message_id + param->client_uid;

    while (1)
    {
        if (send(param->fd, raw, param->bytes, 0) <= 0)
        {
            failed(raw, param->bytes, param->message_id, param->fd, 1);
            break;
        }
        else
        {
            if (!param->session_path.empty())
                std::remove(param->session_path.c_str());
            if (!param->info_path.empty())
                std::remove(param->info_path.c_str());
        }
        sleep(qos1_wait_time);
        if (messages->find(key) != messages->end())
        {
            messages->erase(key);
            break;
        }

        raw[0] |= 0x08; // 把dup位设为1，表示该消息是重复发送的
    }
}

/**
 * 通过qos2向订阅者发送发布者的消息，类似qos1，但qos2需要处理更多细节
*/
void send_qos2(void *arg)
{
    SendQos *param = (SendQos *)arg;
    u_char *raw = param->data;
    uint32_t key = param->message_id + param->client_uid;
    while (1)
    {
        // Qos2第一阶段(Broker作为发送方)，此阶段原样转发发送者的包
        if (send(param->fd, raw, param->bytes, 0) <= 0)
        {
            failed(raw, param->bytes, param->message_id, param->fd, 2);
            return;
        }

        sleep(qos2_wait_time);
        if (messages->find(key) != messages->end())
        {
            // 收到PUBREC, 回复PUBREL
            u_char *pub_rel = new u_char[4];
            pub_rel[0] = PUBREL;
            pub_rel[1] = 2; // 剩余长度
            copy_swap(pub_rel + 2, &param->message_id);

            while (1)
            {

                if (send(param->fd, pub_rel, 4, 0) <= 0)
                {
                    failed(raw, param->bytes, param->message_id, param->fd, 2);
                    return;
                }
                sleep(qos2_wait_time);
                if (messages->find(key) == messages->end())
                {
                    // 在接收的过程中，收到了PUBCOMP包后会删除消息ID，因此只需要在这里循环检测消息ID还存不存在即可
                    // 如果已经被删除则表客户端回复了PUBCOMP,不需要再次发送PUBREL，直接退出即可
                    delete pub_rel;
                    if (!param->session_path.empty())
                        std::remove(param->session_path.c_str());
                    if (!param->info_path.empty())
                        std::remove(param->info_path.c_str());
                    return;
                }
            }
        }

        raw[0] |= 0x08; // 把dup位设为1，表示该消息是重复发送的
    }
}

void accept_clien_connect(void *arg)
{

    sock_addr_info client;
    socklen_t len = sizeof(client);
    Msg *msg = (Msg *)arg;
    struct epoll_event ev;
    int cfd = accept(msg->lfd, (sock_addr *)&client, &len);
    ev.events = EPOLLIN;
    ev.data.fd = cfd;
    int flag = fcntl(cfd, F_GETFL); // 获取文件描述符的当前属性
    flag |= O_NONBLOCK;             // 设置文件描述符为非阻塞模式
    fcntl(cfd, F_SETFL, flag);
    epoll_ctl(msg->epfd, EPOLL_CTL_ADD, cfd, &ev); // 将设置好非阻塞属性的文件描述符加入EPOLL树中
    delete msg;
}

void handle_message(void *arg)
{
    // 使用无符号单字节，有符号的char最大限制为127，其实用char也不影响，只是调试的时候不方便看值
    // 因为char如果超过了127，会被解析成补码
    u_char buff[1024];
    Msg *msg = (Msg *)arg;
    bool kepp_alive = false;
    void checkout_session(int);

    WillMessage *will_msg = new WillMessage;
    ssize_t will_size = 0;
    will_msg->inited = false;

    while (true)
    {

        // 接收客户端消息
        ssize_t bytes = recv(msg->lfd, buff, sizeof(buff), 0);

        if (bytes == 0)
        {
            // 客户端主动断开连接或超时，从epoll树上删除文件描述符
            // Client disconnected,remove the file descriptor from the epoll three

            if (kepp_alive)
            {
                // kepp_alive超时，检查遗嘱消息
                if (will_msg->inited)
                {
                    u_char *publish_data = package_will_data(will_msg, will_size);
                    if (will_size)
                    {
                        do_publish(publish_data, will_size, msg->lfd);
                    }

                    delete will_msg;
                }
            }
            epoll_ctl(msg->epfd, EPOLL_CTL_DEL, msg->lfd, nullptr);
            close(msg->lfd);
            break;
        }
        else if (bytes == -1)
        {
            // 客户端异常中断
            if (errno == EAGAIN)
            {
                break;
            }
            else
            {
                delete msg;
                msg = nullptr;
                return;
            }
        }
        else
        {

            ssize_t nretbytes = 0;
            protocol_paser(buff, bytes, &nretbytes, &kepp_alive, will_msg, msg->lfd);
            if (nretbytes > 0)
            {
                ssize_t send_bytes = send(msg->lfd, buff, nretbytes, 0);
                if (buff[0] == CONNACK && send_bytes)
                {
                    // 回复CONNACK后检查是否有断开期间的未读消息(需要连接时关闭clen_session字段)
                    checkout_session(msg->lfd);
                }
            }
        }
    }

    delete msg;
}

void get_session_files(const std::string &path, std::set<std::string> &files)
{
    DIR *dirp;
    struct dirent *dp;
    dirp = opendir(path.c_str());
    std::string::size_type idx;

    std::cout << "path=" << path << std::endl;

    if (dirp == nullptr)
    {
        std::cout << "opening " << path << " failed" << std::endl;
        return;
    }

    while ((dp = readdir(dirp)) != nullptr)
    {
        std::string curpath(path);
        if (path.back() != '/')
        {
            curpath += '/';
        }
        curpath += dp->d_name;
        files.insert(curpath);
    }
    closedir(dirp);
}

/**
 * 检查断开期间是否有未接收的消息, 仅clean_session为0时可用
 */
void checkout_session(int fd)
{

    std::string client_id;

    for (auto &it : *save_session_map)
    {
        if (it.second == fd)
            client_id = it.first;
    }

    if (client_id.empty())
        return;

    std::cout << "checking session...\n";

    sock_addr_info client;
    socklen_t clen = sizeof(client);
    getpeername(fd, (sock_addr *)&client, &clen);
    uint32_t cliet_uid = client.sin_addr.s_addr + client.sin_port;
    std::string path = default_storage_path;
    path.append(client_id);

    std::set<std::string> files;
    get_session_files(path, files);
    for (auto &elem : files)
    {
        std::string::size_type idx = elem.find("info");

        if (idx == std::string::npos) // 跳过不是消息属性的文件
            continue;

        std::ifstream in(elem, std::ios::in | std::ios::binary);
        if (!in)
        {
            std::cout << "cannot read " << elem << std::endl;
            continue;
        }

        SaveSessionInfo s;
        in.read(reinterpret_cast<char *>(&s), sizeof(s));
        std::string info = elem;
        std::string data_path = info.replace(idx - 1, 5, "");
        u_char *data = new u_char[s.length];
        std::ifstream msg_in(data_path, std::ios::in | std::ios::binary);
        msg_in.read(reinterpret_cast<char *>(data), s.length);

        if (!msg_in)
        {
            std::cout << "cannot read " << data_path << std::endl;
            continue;
        }

        std::shared_ptr<SendQos> param = std::make_shared<SendQos>();
        param->fd = fd;
        param->session_path = data_path;
        param->info_path = elem;
        param->data = data;

        // 获取消息ID
        std::string::size_type pos = elem.find_last_of("/");
        std::string sub_str = elem.substr(pos + 1, path.length() - 1);
        std::string msg_id = sub_str.substr(0, sub_str.find_first_of(".")); // 消息ID的字符串
        param->message_id = cliet_uid + std::stoi(msg_id);

        std::cout << "target qos:" << s.target_qos << std::endl;
        switch (s.target_qos)
        {
        case 0:
            if (send(fd, data, s.length, 0) > 0)
            {
                std::remove(elem.c_str());
                std::remove(data_path.c_str());
                delete data;
            }
            break;
        case 1:

            threadpool->put_task(send_qos1, &param, send_qos1_key);
            break;

        case 2:
            threadpool->put_task(send_qos2, &param, send_qos2_key);
            break;

        default:
            break;
        }

        in.close();
        msg_in.close();
    }
}

void start_run(ConnectionObject *connection, Configuration *config)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    sock_addr_info sinfo;
    socklen_t slen;
    
    const char* addr;
    in_port_t port;

    if (connection == nullptr)
    {
        addr = config->at("listen").c_str();
        port = std::stoi(config->at("port"));
        qos1_wait_time = std::stoi(config->at("qos1_wait_time"));
        qos2_wait_time = std::stoi(config->at("qos2_wait_time"));
        max_thread_num = std::stoi(config->at("max_thread_num"));
        min_thread_num = std::stoi(config->at("min_thread_num"));
        task_queque_size = std::stoi(config->at("task_queque_size"));
    }
    else
    {
        addr = connection->get_ip4addr();
        port = connection->get_port();
    }

    std::cout<< addr<<std::endl;

    sinfo.sin_addr.s_addr = inet_addr(addr);
    sinfo.sin_family = AF_INET;
    sinfo.sin_port = htons(port);
    slen = sizeof(sinfo);
    if (bind(lfd, (sock_addr *)&sinfo, slen) == -1)
    {
        perror("failed to bind");
        exit(1);
    }

    if (listen(lfd, LISTEN_LENGTH) == -1)
    {
        perror("failed to listen");
        exit(1);
    }
    int epfd = epoll_create(1); // 创建epoll句柄
    struct epoll_event ev;
    ev.data.fd = lfd;
    ev.events = EPOLLIN;

    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    struct epoll_event evs[512];
    int size = sizeof(evs) / sizeof(evs[0]);

    client_map = new std::map<std::string, std::map<int, Subscriber>>;
    messages = new std::set<uint32_t>;
    save_session_map = new std::map<std::string, int>;
    pthread_mutex_init(&mutex_map, nullptr); // 初始化信号量
    pthread_mutex_init(&mutex_session, nullptr);

    threadpool = new ThreadPool(max_thread_num, min_thread_num, task_queque_size); // 线程池

    while (1)
    {
        int count = epoll_wait(epfd, evs, size, -1);
        for (int i = 0; i < count; ++i)
        {
            int fd = evs[i].data.fd;
            Msg *msg = new Msg(epfd, fd);
            if (fd == lfd)
            {
                msg->fn_key = accept_conncet_key;
                threadpool->put_task(accept_clien_connect, msg, accept_conncet_key);
            }
            else
            {
                msg->fn_key = handle_message_key;
                threadpool->put_task(handle_message, msg, handle_message_key);
            }
        }
    }

    pthread_mutex_destroy(&mutex_map);
    pthread_mutex_destroy(&mutex_session);
    client_map->clear();
    delete client_map;
    messages->clear();
    delete messages;
}
