#include "../include/CookieParser.hpp"

CookieParser::CookieParser() : _maxAge(-1), _httpOnly(true), _sameSite("Lax"), _path("/") {}

CookieParser::CookieParser(const CookieParser& other) {
    *this = other;
}

CookieParser::~CookieParser() {}

CookieParser& CookieParser::operator=(const CookieParser& other) {
    if (this != &other) {
        _maxAge = other._maxAge;
        _httpOnly = other._httpOnly;
        _sameSite = other._sameSite;
        _path = other._path;
    }
    return *this;
}

std::string CookieParser::trim(const std::string& s) {
    const std::string whitespace = " \t\r\n";

    std::string::size_type start = s.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";

    std::string::size_type end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

std::map<std::string, std::string> CookieParser::parse(const std::string& headerValue) {
    std::map<std::string, std::string> cookies;

    std::istringstream stream(headerValue);
    std::string token;

    while (std::getline(stream, token, ';')) {
        std::string::size_type eq = token.find('=');

        if (eq == std::string::npos)
            continue;

        std::string name  = trim(token.substr(0, eq));
        std::string value = trim(token.substr(eq + 1));

        if (!name.empty())
            cookies[name] = value;
    }

    return cookies;
}

std::string CookieParser::build(const std::string& name, const std::string& value) {
        std::ostringstream oss;

        oss << name << "=" << value;
        oss << "; Path=" << this->_path;

        if (this->_maxAge >= 0)
            oss << "; Max-Age=" << this->_maxAge;
        if (this->_httpOnly)
            oss << "; HttpOnly";
        if (!this->_sameSite.empty())
            oss << "; SameSite=" << this->_sameSite;
        return oss.str();
}

std::string CookieParser::buildExpired(const std::string& name) {
        this->_maxAge   = 0;
        this->_httpOnly = true;
        return this->build(name, "");
}
