#ifndef FILEREGISTRY_HPP
# define FILEREGISTRY_HPP

# include <map>
# include <string>
# include <vector>

class FileRegistry {

private:

    std::map<std::string, std::vector<std::string> > _filesByUser;

    FileRegistry();
    FileRegistry(const FileRegistry& other);
    FileRegistry& operator=(const FileRegistry& other);

public:

    static FileRegistry& getInstance();

    ~FileRegistry();

    void                        registerFile(const std::string& username, const std::string& path);
    void                        unregisterFile(const std::string& username, const std::string& path);
    std::vector<std::string>    getFiles(const std::string& username) const;
    bool                        hasFiles(const std::string& username) const;
    void                        clearUser(const std::string& username);
    bool                        isOwner(const std::string& username, const std::string& path);

};

#endif
