#pragma once
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

typedef struct sockaddr sock_addr;
typedef struct sockaddr_in sock_addr_info;

typedef struct __SendQos
{
    int fd;
    u_char *data;
    std::string client_id;
    std::string session_path;
    std::string info_path;
    uint16_t message_id;
    uint32_t client_uid;
    ssize_t bytes;
}SendQos;

typedef struct __SaveSession
{
    uint16_t length;
    int fd;
    int target_qos;
}SaveSessionInfo;

class WillMessage
{
private:

    u_char will_qos;
    bool will_retain;
    std::string will_topic;
    char* palyload;
    ssize_t payload_size;
    // std::shared_ptr<u_char> payload;
    
public:
    bool inited;
    WillMessage();
    ~WillMessage();
    void set_will_qos(u_char qos);
    u_char get_will_qos();
    void set_will_retain(bool will_retain);
    bool get_will_retain();
    void set_will_topic(std::string will_topic);
    std::string get_will_topic();
    void set_payload(char* payload);
    bool get_payload(u_char* mem,size_t bytes);
    void set_payload_szie(ssize_t size);
    ssize_t get_payload_szie();
};


class Subscriber
{
private:
    int qos;
    std::string client_id;

public:
    int tfd;
    uint32_t uid;       //客户端标识符，使用IP地址+端口号相加
    uint16_t message_ident;
    
    Subscriber(int m_fd, int m_qos) : tfd(m_fd), qos(m_qos){};
    Subscriber();
    ~Subscriber();

    void set_qos(int qos);
    void set_client_id(std::string cliend_id);
    int get_fd();
    int get_qos();
};


class ConnectionObject
{
private:
    const char *ip4_addr;
    in_port_t port;
    
public:
    ConnectionObject();
    ConnectionObject(const char *m_addr, in_port_t m_port) : ip4_addr(m_addr), port(m_port) {}
    ~ConnectionObject();
    const char*  get_ip4addr();
    in_port_t get_port();
};

