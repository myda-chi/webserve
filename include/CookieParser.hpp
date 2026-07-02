#ifndef COOKIE_PARSER_HPP
# define COOKIE_PARSER_HPP

#include <map>
#include <string>
#include <sstream>

class CookieParser {

private:

    int         _maxAge;
    bool        _httpOnly;
    std::string _sameSite;
    std::string _path;

    std::string trim(const std::string& s);
    
public:
    
    CookieParser();
    CookieParser(const CookieParser& other);
    ~CookieParser();

    CookieParser& operator=(const CookieParser& other);

    std::map<std::string, std::string>   parse(const std::string& headerValue);
    std::string                          build(const std::string& name, const std::string& value);
    std::string                          buildExpired(const std::string& name);

};

#endif