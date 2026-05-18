#include "../include/Utils.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <ctime>

namespace Utils {
	// String manipulation
	std::string trim(const std::string& str) 
	{
		size_t start = str.find_first_not_of(" \t\r\n");
		if(start == std::string::npos)
			return "";
		size_t end = str.find_last_not_of(" \t\r\n");
		return(str.substr(start, end - start + 1));
	}

	std::string toLower(const std::string& str) 
	{
		std::string out = str;
		for(size_t i = 0; i < out.size(); i++)
		{
			out[i] = std::tolower(out[i]);
		}
		return(out);
	}

	std::string toUpper(const std::string& str) 
	{
		std::string out = str;
		for(size_t i =0; i < out.size(); i++)
		{
			out[i] = std::toupper(out[i]);
		}
		return(out);
	}

	std::vector<std::string> split(const std::string& str, char delimiter) {
		// TODO: Implementation
		(void)str;
		(void)delimiter;
		return std::vector<std::string>();
	}

	std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
		// TODO: Implementation
		(void)str;
		(void)delimiter;
		return std::vector<std::string>();
	}

	std::string join(const std::vector<std::string>& vec, const std::string& delimiter) {
		// TODO: Implementation
		(void)vec;
		(void)delimiter;
		return "";
	}

	bool startsWith(const std::string& str, const std::string& prefix) {
		// TODO: Implementation
		(void)str;
		(void)prefix;
		return false;
	}

	bool endsWith(const std::string& str, const std::string& suffix) {
		// TODO: Implementation
		(void)str;
		(void)suffix;
		return false;
	}

	// Type conversion
	std::string toString(int value) 
	{
		std::stringstream ss;
		ss << value;
		return(ss.str());
	}

	std::string toString(size_t value) 
	{
		std::stringstream ss;
		ss << value;
		return(ss.str());
	}

	std::string toString(long value) 
	{
		std::stringstream ss;
		ss << value;
		return(ss.str());
	}

	int toInt(const std::string& str) 
	{
		int val;
		std::stringstream ss(str);
		ss >> val;
		return(val);
	}

	size_t toSize(const std::string& str) 
	{
		size_t val;
		std::stringstream ss(str);
		ss >> val;
		return(val);
	}

	// File operations
	bool fileExists(const std::string& path) 
	{
		std::ifstream file(path.c_str());
		return(file.is_open());
	}

	bool isDirectory(const std::string& path) 
	{
		struct stat st;
		if(::stat(path.c_str(), &st) != 0)
			return false;
		return S_ISDIR(st.st_mode);
	}

	bool isFile(const std::string& path) 
	{
		struct stat st;
		if(::stat(path.c_str(), &st) != 0)
			return false;
		return S_ISREG(st.st_mode);
	}

	bool isReadable(const std::string& path) 
	{
		std::ifstream file(path.c_str());
		return file.good();
	}

	std::string readFile(const std::string& path) 
	{
		std::ifstream file(path.c_str(), std::ios::binary);
    	std::ostringstream ss;
    	ss << file.rdbuf();
    	return ss.str();
	}

	bool writeFile(const std::string& path, const std::string& content) 
	{
		std::ofstream file(path.c_str(), std::ios::binary);
    	if (!file.is_open())
        	return false;
    	file << content;
    	return file.good();
	}

	std::string getFileExtension(const std::string& path) 
	{
		size_t dot = path.rfind('.');
    	if (dot == std::string::npos)
        	return "";
    	return path.substr(dot + 1);
	}

	std::string getFileName(const std::string& path) 
	{
		size_t slash = path.rfind('/');
    	if (slash == std::string::npos)
        	return path;
    	return path.substr(slash + 1);
	}

	std::string getDirectory(const std::string& path) 
	{
		size_t slash = path.rfind('/');
		if (slash == std::string::npos)
			return "./";
		return path.substr(0, slash + 1);
	}

	std::vector<std::string> listDirectory(const std::string& path) 
	{
		std::vector<std::string> entries;
    	DIR* dir = opendir(path.c_str());
    	if (dir == NULL)
        	return entries;
    	struct dirent* entry;
    	while ((entry = readdir(dir)) != NULL) {
        	std::string name(entry->d_name);
        	if (name != "." && name != "..")
            	entries.push_back(name);
    }
    closedir(dir);
    return entries;
	}

	// URL/Path operations
	std::string urlDecode(const std::string& str) 
	{
		std::string result;
    	for (size_t i = 0; i < str.size(); ++i) {
        	if (str[i] == '%' && i + 2 < str.size()) {
            	// take the two chars after % and convert from hex to a character
            	std::string hex = str.substr(i + 1, 2);
            	char decoded = static_cast<char>(std::strtol(hex.c_str(), NULL, 16));
            	result += decoded;
            	i += 2; // skip the two hex chars
        	} 
			else if (str[i] == '+') {
            	result += ' '; // + means space in URL encoding
        	} 
			else {
            	result += str[i];
        }
    }
    return result;
	}

	std::string urlEncode(const std::string& str) 
	{
		std::ostringstream encoded;
    	for (size_t i = 0; i < str.size(); ++i) {
        	char c = str[i];
        	// letters, digits and these special chars are safe — everything else gets encoded
        	if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            	encoded << c;
        	} else {
            	encoded << '%' << std::uppercase << std::hex
                    << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return encoded.str();
	}

	std::string normalizePath(const std::string& path) 
	{
		std::vector<std::string> parts;
    	std::istringstream ss(path);
    	std::string token;

		while (std::getline(ss, token, '/')) {
			if (token == "" || token == ".")
				continue;          // skip empty parts and "current dir" dots
			else if (token == "..") {
				if (!parts.empty())
					parts.pop_back(); // go up one level
			} else {
				parts.push_back(token);
			}
		}
	}

	std::string joinPath(const std::string& path1, const std::string& path2) 
	{
		if (path1.empty())
        	return path2;
		if (path2.empty())
			return path1;

		// avoid double slashes at the join point
		bool p1HasSlash = path1[path1.size() - 1] == '/';
		bool p2HasSlash = path2[0] == '/';

		if (p1HasSlash && p2HasSlash)
			return path1 + path2.substr(1);  // remove one slash
		if (!p1HasSlash && !p2HasSlash)
			return path1 + "/" + path2;      // add missing slash
		return path1 + path2;               // already correct
	}

	std::string resolvePath(const std::string& root, const std::string& path) 
	{
		std::string joined = joinPath(root, path);
		std::string normalized = normalizePath(joined);

		// security check — make sure final path still starts with root
		if (normalized.find(normalizePath(root)) != 0)
			return "";  // path tried to escape root → block it

		return normalized;
	}

	// MIME types
	std::string getMimeType(const std::string& extension) 
	{
		// TODO: Implementation
		(void)extension;
		return "";
	}

	std::string getDefaultMimeType() 
	{
		return "application/octet-stream";
	}

	// Date/Time
	std::string getHttpDate() 
	{
		// TODO: Implementation
		return "";
	}

	std::string getHttpDate(time_t time) 
	{
		// TODO: Implementation
		(void)time;
		return "";
	}

	// HTTP utilities
	std::string getStatusMessage(int statusCode) 
	{
		switch(statusCode)
		{
			case 200: return "OK";
			case 201: return "Created";
			case 204: return "No Content";
			case 301: return "Moved Permanently";
			case 400: return "Bad Request";
			case 403: return "Forbidden";
			case 404: return "Not Found";
			case 405: return "Method Not Allowed";
			case 413: return "Payload Too Large";
			case 500: return "Internal server Error";
			default: return "Unknown";
		}
	}

	bool isValidMethod(const std::string& method) 
	{
		// TODO: Implementation
		(void)method;
		return false;
	}

	bool isValidStatusCode(int code) 
	{
		// TODO: Implementation
		(void)code;
		return false;
	}

	// Encoding
	std::string base64Encode(const std::string& input) 
	{
		// TODO: Implementation
		(void)input;
		return "";
	}

	std::string base64Decode(const std::string& input) 
	{
		// TODO: Implementation
		(void)input;
		return "";
	}

	std::string hexEncode(const std::string& input) 
	{
		// TODO: Implementation
		(void)input;
		return "";
	}

	std::string hexDecode(const std::string& input) 
	{
		// TODO: Implementation
		(void)input;
		return "";
	}
}
