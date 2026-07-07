#include "../include/FileRegistry.hpp"

FileRegistry::FileRegistry() {}

FileRegistry::~FileRegistry() {}

FileRegistry& FileRegistry::getInstance() {
    static FileRegistry instance;
    return instance;
}

void FileRegistry::registerFile(const std::string& username, const std::string& path) {
    if (username.empty() || path.empty())
        return;

    std::vector<std::string>& files = _filesByUser[username];
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i] == path)
            return;
    }
    files.push_back(path);
}

void FileRegistry::registerFile(const std::string& path) {
    registerFile("anonymous", path);
}

void FileRegistry::unregisterFile(const std::string& username, const std::string& path) {
    std::map<std::string, std::vector<std::string> >::iterator it = _filesByUser.find(username);
    if (it == _filesByUser.end())
        return;

    std::vector<std::string>& files = it->second;
    for (std::vector<std::string>::iterator file = files.begin(); file != files.end(); ++file) {
        if (*file == path) {
            files.erase(file);
            break;
        }
    }
    if (files.empty())
        _filesByUser.erase(it);
}

void FileRegistry::unregisterFile(const std::string& path) {
    unregisterFile("anonymous", path);
}

std::vector<std::string> FileRegistry::getFiles(const std::string& username) const {
    std::map<std::string, std::vector<std::string> >::const_iterator it = _filesByUser.find(username);
    if (it == _filesByUser.end())
        return std::vector<std::string>();
    return it->second;
}

bool FileRegistry::hasFiles(const std::string& username) const {
    std::map<std::string, std::vector<std::string> >::const_iterator it = _filesByUser.find(username);
    return it != _filesByUser.end() && !it->second.empty();
}

void FileRegistry::clearUser(const std::string& username) {
    _filesByUser.erase(username);
}
