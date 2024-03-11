#ifndef PROTOCOL_H
#define PROTOCOL_H

#define TIMEOUT   3

#define LISTEN_LENGTH 64

#define PROTOCOL_LENGTH 4

#define RETCODE 0
/*固定报文第一个字节  the first byte of fixed header */
#define RESERVERD 0

#define CONNECT 0x10 // 客户端连接Broker请求        Cliet conncet to Broker request

#define CONNACK 0x20 // Broker回复客户端连接确认    Broker connected response

#define PUBLISH 0x30 // 发布消息，双向，DUP=0,Qos1=0,Qos2=0,retain=0

#define PUBACK 0x40 // 发布确认,双向 (Qos1使用)     Published, only use in Qos1

#define PUBREC 0x50 // 消息已接收，双向 ，Qos2第一阶段  recived, only use in Qos2 stage 1

#define PUBREL 0x62 // 消息释放，双向，Qos2第二阶段

#define PUBCOMP 0x70 // 发布结束，双向，Qos2第三阶段

#define SUBSCRIBE 0x82 // 客户端订阅请求     Clinet subscribe request

#define SUBACK 0x90 // Broker订阅确认     Broker subscribed response

#define UNSUBSCRIBE 0xA2 // 客户端取消订阅     Client unsubscribe request

#define UNSUBACK 0xB0 // Broker取消订阅回复

#define PINGREQ 0xC0 // 客户端心跳PING请求  Clinet keep-alive Ping requset

#define PINGRESP 0xD0 // Broker PING回复    Broker PING response

#define DISCONNECT 0xE0 // 客户端断开连接请求   Client disconnect request

#define PUB_CHECK(X) (X >= PUBLISH && X <= 0x3F) ? PUBLISH : X // 检测是否为发布类型的报文

#include "object.h"
#include "configuration.h"
void start_run(ConnectionObject* connection,Configuration* config);
#endif






