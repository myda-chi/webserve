#include "../include/SessionMiddleware.hpp"

const std::string SessionMiddleware::_cookieName = "SESSID";

SessionMiddleware::SessionMiddleware() {}

SessionMiddleware::SessionMiddleware(const SessionMiddleware& other) {
    (void)other;
}

SessionMiddleware::~SessionMiddleware() {}

SessionMiddleware& SessionMiddleware::operator=(const SessionMiddleware& other) {
    (void)other;
    return *this;
}

void SessionMiddleware::processRequest(HttpRequest& request) {
    SessionManager& manager = SessionManager::getInstance();
    CookieParser parser;
    std::string rawCookie = request.getHeader("Cookie");
    std::string sessionId = "";
    if (!rawCookie.empty()) {
        std::map<std::string, std::string> cookies = parser.parse(rawCookie);
        std::map<std::string, std::string>::iterator it = cookies.find(_cookieName);
        if (it != cookies.end())
            sessionId = it->second;
    }
    if (!sessionId.empty()) {
        Session* session = manager.getSession(sessionId);
        if (session) {
            request.setSessionId(sessionId);
            request.setSession(session);
            return;
        }
    }
    std::string newSessionId = manager.createSession();
    Session* newSession = manager.getSession(newSessionId);
    request.setSessionId(newSessionId);
    request.setSession(newSession);
}

void SessionMiddleware::processResponse(HttpRequest& request, HttpResponse& response) {
    CookieParser parser;
    std::string rawCookie = request.getHeader("Cookie");
    std::string inComingSessionId;
    if (!rawCookie.empty()) {
        std::map<std::string, std::string> cookies = parser.parse(rawCookie);
        std::map<std::string, std::string>::iterator it = cookies.find(_cookieName);
        if (it != cookies.end())
            inComingSessionId = it->second;
    }
    if (inComingSessionId != request.getSessionId() && !request.getSessionId().empty())
        response.setCookieHeader(parser.build(_cookieName, request.getSessionId()));
}
