#ifndef SESSION_HPP
# define SESSION_HPP

#include <map>
#include <string>
#include <ctime>
#include <iomanip>
#include <cstdio>
#include <sstream>
#include <cstdlib>

class Session {

private:

    std::map<std::string, std::string>  _data;
    std::time_t                         _lastAccessed;

public:

    Session();
    Session(const Session& other);
    ~Session();

    Session& operator=(const Session& other);

    bool            hasKey(const std::string& key) const;
    std::string     getValue(const std::string& key) const;
    std::time_t     getLastAccessed() const;
    void            setData(const std::string& key, const std::string& value);
    void            unsetData(const std::string& key);
    void            touch();
    
};

class SessionManager {
    
private:
    
    std::map<std::string, Session> _sessions;
    static const int TTL = 300; // 5 minutes

    SessionManager();
    SessionManager(const SessionManager& other);
    SessionManager& operator=(const SessionManager& other);

    std::string generateSessionId();
    bool isExpired(const Session& session) const;

public:

    static SessionManager& getInstance();

    ~SessionManager();
    
    std::string     createSession();
    Session*        getSession(const std::string& id);
    void            destroySession(const std::string& sessionId);
    void            purgeExpiredSessions();
    
};

#endif