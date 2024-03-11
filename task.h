#pragma onces
#define _Task Task

class Task
{
public:
    void (*entrance)(void *arg);
    void *arg;
    int key;
};

class Msg
{
public:
    int epfd;
    int lfd;
    int fn_key;
    Msg(int m_epfd, int m_lfd) : epfd(m_epfd), lfd(m_lfd){};
    ~Msg(){};
};

