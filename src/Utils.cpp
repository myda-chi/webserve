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
		std::vector<std::string> tokens;
		std::istringstream ss(str);
		std::string token;

		while(std::getline(ss, token, delimiter))
		{
			if(!token.empty())
				tokens.push_back(token);
		}
		return tokens;
	}

	std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
		std::vector<std::string> tokens;
		size_t start = 0;
		size_t end = str.find(delimiter);

		while(end != std::string::npos)
		{
			std::string token = str.substr(start, end - start);
			if(!token.empty())
				tokens.push_back(token);
			start = end + delimiter.size();
			end = str.find(delimiter, start);
		}

		std::string last = str.substr(start);
		if(!last.empty())
			tokens.push_back(last);
			
		return tokens;
	}

	std::string join(const std::vector<std::string>& vec, const std::string& delimiter) {
		std::string result;

		for(size_t i = 0; i < vec.size(); ++i)
		{
			result += vec[i];
			if(i + 1 < vec.size())
				result += delimiter;
		}
		return result;
	}

	bool startsWith(const std::string& str, const std::string& prefix) {
		if(prefix.size() > str.size())
			return false;
		return str.compare(0, prefix.size(), prefix) == 0;
	}

	bool endsWith(const std::string& str, const std::string& suffix) {
		if(suffix.size() > str.size())
			return false;
		return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
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
		if (extension == "html" || extension == "htm")
			return "text/html";
		if (extension == "css")
			return "text/css";
		if (extension == "js")
			return "application/javascript";
		if (extension == "json")
			return "application/json";
		if (extension == "txt")
			return "text/plain";
		if (extension == "png")
			return "image/png";
		if (extension == "jpg" || extension == "jpeg")
			return "image/jpeg";
		if (extension == "gif")
			return "image/gif";
		if (extension == "ico")
			return "image/x-icon";
		if (extension == "pdf")
			return "application/pdf";
		if (extension == "mp4")
			return "video/mp4";
		if (extension == "mp3")
			return "audio/mpeg";
		return "application/octet-stream"; // unknown → raw binary
	}

	std::string getDefaultMimeType() 
	{
		return "application/octet-stream";
	}

	// Date/Time
	std::string getHttpDate() 
	{
		return getHttpDate(std::time(NULL));
	}

	std::string getHttpDate(time_t time) 
	{
		struct tm* gmt = std::gmtime(&time);
		char buf[64];
		// HTTP date format: "Mon, 04 Jun 2026 10:00:00 GMT"
		std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
		return std::string(buf);
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
		return method == "GET"    ||
           method == "POST"   ||
           method == "DELETE" ||
           method == "HEAD"   ||
           method == "PUT";
	}

	bool isValidStatusCode(int code) 
	{
		return (code >= 100 && code <= 599);
	}

	// Encoding
	std::string base64Encode(const std::string& input) 
	{
		const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string result;
		int val = 0;
		int valb = -6;

		for (size_t i = 0; i < input.size(); ++i) {
			val = (val << 8) + static_cast<unsigned char>(input[i]);
			valb += 8;
			while (valb >= 0) {
				result.push_back(chars[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}

		// add padding '=' if needed
		if (valb > -6)
			result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
		while (result.size() % 4)
			result.push_back('=');

		return result;
	}

	std::string base64Decode(const std::string& input) 
	{
		const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string result;
		std::vector<int> T(256, -1);

		// build lookup table
		for (int i = 0; i < 64; ++i)
			T[static_cast<unsigned char>(chars[i])] = i;

		int val = 0;
		int valb = -8;

		for (size_t i = 0; i < input.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(input[i]);
			if (T[c] == -1)
				break; // invalid or padding character
			val = (val << 6) + T[c];
			valb += 6;
			if (valb >= 0) {
				result.push_back(static_cast<char>((val >> valb) & 0xFF));
				valb -= 8;
			}
		}
		return result;
	}

	std::string hexEncode(const std::string& input) 
	{
		const char* hexChars = "0123456789abcdef";
		std::string result;

		for (size_t i = 0; i < input.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(input[i]);
			result.push_back(hexChars[c >> 4]);   // high 4 bits
			result.push_back(hexChars[c & 0x0F]); // low 4 bits
		}
		return result;
	}

	std::string hexDecode(const std::string& input) 
	{
		std::string result;

		for (size_t i = 0; i + 1 < input.size(); i += 2) {
			// convert each pair of hex chars back to a byte
			std::string byte = input.substr(i, 2);
			char c = static_cast<char>(std::strtol(byte.c_str(), NULL, 16));
			result.push_back(c);
		}
		return result;
	}
}
