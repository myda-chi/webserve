#ifndef SESSIONMIDDLEWARE_HPP
# define SESSIONMIDDLEWARE_HPP

#include <string>
#include "SessionManager.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "CookieParser.hpp"

class SessionMiddleware {

private:
    
    static const std::string _cookieName;

public:

    SessionMiddleware();
    SessionMiddleware(const SessionMiddleware& other);
    ~SessionMiddleware();

    SessionMiddleware& operator=(const SessionMiddleware& other);

    void    processRequest(HttpRequest& request);
    void    processResponse(HttpRequest& request, HttpResponse& response);

};

#endif