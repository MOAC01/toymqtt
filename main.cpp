#include "protocol.h"
#include "configuration.h"
#include <unistd.h>
const char *default_conf_path = "/etc/toymqtt/toymqtt.conf"; // 默认配置文件路径
const char *help = "Usage: toymqtt [address] [port] to start  broker\n \      
                        Examples:\n\
                       toymqtt  127.0.0.1  1833\n";

int main(int argc, char const *argv[])
{

    if (argc > 2)
    {
        // 从命令行读取配置
        ConnectionObject object(argv[1], (in_port_t)atoi(argv[2]));
        start_run(&object, nullptr);
    }

    
    else
    {
        // 从配置文件读取配置
        const char *path = argc == 2 ? argv[1] : default_conf_path;
        Configuration config(path);
        start_run(nullptr, &config);
        if (config.get_ret())
            start_run(nullptr, &config);
        else
        {
            std::cout << config.get_errmsg() << std::endl;
            exit(0);
        }
    }

    return 0;
}
