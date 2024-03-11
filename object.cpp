#include "object.h"

Subscriber::Subscriber()
{
}

Subscriber::~Subscriber()
{
}

void Subscriber::set_qos(int qos)
{
    this->qos = qos;
}

void Subscriber::set_client_id(std::string cliend_id)
{
    this->client_id = client_id;
}

int Subscriber::get_fd()
{
    return this->tfd;
}

int Subscriber::get_qos()
{
    return this->qos;
}

ConnectionObject::ConnectionObject()
{

}

ConnectionObject::~ConnectionObject()
{
    delete[] this->ip4_addr;
}


const char* ConnectionObject::get_ip4addr()
{
    return this->ip4_addr;
}

in_port_t ConnectionObject::get_port()
{
    return this->port;
}

WillMessage::WillMessage()
{

}
WillMessage:: ~WillMessage()
{
    delete[] this->palyload;
}

void WillMessage::set_will_qos(u_char qos)
{
    this->will_qos = qos;
}

u_char WillMessage::get_will_qos()
{
    return this->will_qos;
}

void WillMessage::set_will_retain(bool will_retain)
{
    this->will_retain = will_retain;

}

bool WillMessage::get_will_retain()
{
    return this->will_retain;
}

void WillMessage::set_will_topic(std::string will_topic)
{
    this->will_topic = will_topic;
}

std::string WillMessage::get_will_topic()
{
    return this->will_topic;
}

void WillMessage::set_payload(char* payload)
{
    this->palyload = palyload;

}

bool WillMessage::get_payload(u_char *mem, size_t bytes)
{
    if (nullptr == mem || bytes > this->payload_size)
        return false;

    memcpy(mem,this->palyload,this->payload_size);
    return true;
}

void WillMessage::set_payload_szie(ssize_t size)
{
    this->payload_size = size;
}

ssize_t  WillMessage:: get_payload_szie()
{
    return this->payload_size;
}