#include "configuration.h"
#include <fstream>
#include <regex>
#include <vector>

Configuration::Configuration(std::string m_path)
{
    this->path = m_path;
    insert(std::make_pair("listen", "unknown"));
    insert(std::make_pair("port", "unknown"));
    insert(std::make_pair("qos1_wait_time", "unknown"));
    insert(std::make_pair("qos2_wait_time", "unknown"));
    insert(std::make_pair("max_thread_num", "unknown"));
    insert(std::make_pair("min_thread_num", "unknown"));
    insert(std::make_pair("task_queque_size", "unknown"));
    format_check();
}

Configuration::~Configuration()
{
}

void Configuration::trim(std::string &s)
{
    int index = 0;
    if (!s.empty())
    {
        while ((index = s.find(' ', index)) != std::string::npos)
        {
            s.erase(index, 1);
        }
    }
}
bool Configuration::get_ret()
{
    return ret;
}

std::string Configuration::get_errmsg()
{
    return errmsg;
}
std::string Configuration::get(const std::string key)
{
    auto it = this->find(key);
    if (it != this->end())
        return (*it).second;

    else
        return "unknown";
}

/**
 * 配置文件格式检查(config file format checking)
 *
 */
void Configuration::format_check()
{
    std::ifstream in_file(path);
    std::string line;
    std::map<int, std::string> values_map;
    std::regex ip4_pattern(R"(\d+\.\d+\.\d+\.\d+)");
    std::regex num_pattern(R"(\d+)");
    int i = 0;
    int broker_index = -1;
    int client_index = -1;

    try
    {

        while (getline(in_file, line))
        {
            trim(line);
            if (line.compare("[broker]") == 0)
            {
                broker_index = i;
            }

            else
            {
                std::istringstream ss(line); // 使用字符串构造一个 stringstream 对象
                std::string word;
                std::vector<std::string> vec;
                while (getline(ss, word, '='))
                {
                    if (!word.empty())
                        vec.push_back(word);
                }

                if (!(vec.size() == 2 && this->find(vec[0]) != this->end()))
                {

                    std::string msg;
                    msg.append("option '");
                    msg.append(vec[0]);
                    msg.append("' does not surport or format error\n");
                    this->ret = false;
                    this->errmsg = msg;
                    return;
                }

                else
                {
                    this->erase(vec[0]);
                    this->insert(std::make_pair(vec[0], vec[1]));
                }

                values_map[i++] = line;
            }
        }

        if (broker_index == -1)
        {
            this->ret = false;
            this->errmsg = "format error, '[broker]'  not found\n";
            return;
        }

        else
        {
            if (values_map.find(broker_index - 1) != values_map.end())
            {
                this->ret = false;
                this->errmsg = "format error,there are some broker options over '[broker]'\n";
                return;
            }
        }

        std::string unknown = "unknown";
        std::string listen = get("listen");
        std::string port = get("port");
        std::string qos1_wait_time = get("qos1_wait_time");
        std::string qos2_wait_time = get("qos2_wait_time");
        std::string max_thread_num = get("max_thread_num");
        std::string min_thread_num = get("min_thread_num");
        std::string task_queque_size = get("task_queque_size");

        if (!regex_match(listen, ip4_pattern) && listen != unknown)
        {
            this->ret = false;
            this->errmsg = listen;
            this->errmsg.append(" is not an effective ipv4 address\n");
            return;
        }

        if (port != unknown)
        {
            if (!regex_match(port, num_pattern) && port.size() < 5)
            {
                this->ret = false;
                this->errmsg = "port cannot be a string\n";
                return;
            }
            else
            {
                uint16_t port_num = std::stoi(port);
                if (port_num >= 65535)
                {
                    this->ret = false;
                    this->errmsg = "port must less than 65535\n";
                    return;
                }
            }
        }

        if (!regex_match(qos1_wait_time, num_pattern) && qos1_wait_time != unknown)
        {
            this->ret = false;
            this->errmsg = "qos1_wait_time cannot be a string\n";
            return;
        }

        if (!regex_match(qos2_wait_time, num_pattern) && qos2_wait_time != unknown)
        {
            this->ret = false;
            this->errmsg = "qos2_wait_time cannot be a string\n";
            return;
        }

        if (!regex_match(max_thread_num, num_pattern) && max_thread_num != unknown)
        {
            this->ret = false;
            this->errmsg = "max_thread_num cannot be a string\n";
            return;
        }

        if (!regex_match(max_thread_num, num_pattern) && max_thread_num != unknown)
        {
            this->ret = false;
            this->errmsg = "max_thread_num cannot be a string\n";
            return;
        }

        if (!regex_match(min_thread_num, num_pattern) && min_thread_num != unknown)
        {
            this->ret = false;
            this->errmsg = "min_thread_num cannot be a string\n";
            return;
        }


        if (!regex_match(task_queque_size, num_pattern) && task_queque_size != unknown)
        {
            this->ret = false;
            this->errmsg = "task_queque_size cannot be a string\n";
            return;
        }

        in_file.close();
        this->ret = true;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        this->errmsg = e.what();
        this->ret = false;
    }
}

