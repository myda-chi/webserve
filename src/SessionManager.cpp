#include <../include/SessionManager.hpp>

Session::Session(): _lastAccessed(std::time(NULL)) {}

Session::Session(const Session& other): _data(other._data), _lastAccessed(other._lastAccessed) {}

Session::~Session() {}

Session& Session::operator=(const Session& other) {
    if (this != &other) {
        _data = other._data;
        _lastAccessed = other._lastAccessed;
    }
    return (*this);
}

bool Session::hasKey(const std::string& key) const {
    return _data.find(key) != _data.end();
}

std::string Session::getValue(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = _data.find(key);
    if (it == _data.end())
        return "";
    return it->second;
}

std::time_t Session::getLastAccessed() const {
    return _lastAccessed;
}

void Session::setData(const std::string& key, const std::string& value) {
    _data[key] = value;
}

void Session::unsetData(const std::string& key) {
    _data.erase(key);
}

void Session::touch() {
    _lastAccessed = std::time(NULL);
}

SessionManager::SessionManager() {}

SessionManager::~SessionManager() {}

SessionManager& SessionManager::getInstance() {
    static SessionManager instance;
    return instance;    
}

std::string SessionManager::generateSessionId() {
    unsigned char buf[32];

    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) {
        for (int i = 0; i < 32; i++)
            buf[i] = static_cast<unsigned char>(rand() & 0xff);
    } else {
        std::fread(buf, 1, 32, f);
        std::fclose(f);
    }
    std::ostringstream oss;
    for (int i = 0; i < 32; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buf[i]);
    return oss.str();
}

bool SessionManager::isExpired(const Session& session) const {
    std::time_t now = std::time(NULL);
    return (now - session.getLastAccessed()) > TTL;
}

std::string SessionManager::createSession() {
    purgeExpiredSessions();
    std::string id = generateSessionId();
    _sessions[id] = Session();
    return id;
}

Session* SessionManager::getSession(const std::string& id) {
    std::map<std::string, Session>::iterator it = _sessions.find(id);
    if (it == _sessions.end())
        return NULL;
    if (isExpired(it->second)) {
        _sessions.erase(it);
        return NULL;
    }
    it->second.touch();
    return &it->second;
}

void SessionManager::destroySession(const std::string& sessionId) {
    _sessions.erase(sessionId);
}

void SessionManager::purgeExpiredSessions() {
    std::map<std::string, Session>::iterator it = _sessions.begin();
    while (it != _sessions.end()) {
        if (isExpired(it->second))
            _sessions.erase(it++);
        else
            ++it;
    }
}
