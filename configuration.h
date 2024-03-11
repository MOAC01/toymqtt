#pragma once
#include <iostream>
#include <map>
#include <string>

class Configuration : public std::map<std::string, std::string>
{
private:
   bool ret;
   std::string path;
   std::string errmsg;

protected:
   void format_check();
   void trim(std::string &s);
   std::string get(const std::string key);

public:
   Configuration(std::string m_path);
   ~Configuration();
   bool get_ret();
   std::string get_errmsg();
};
