#ifndef UTILS_HPP
# define UTILS_HPP

# include <string>
# include <vector>
# include <sstream>
# include <algorithm>
# include <fstream>
#include  <dirent.h>  

namespace Utils {
	// String manipulation
	std::string		trim(const std::string& str);
	std::string		toLower(const std::string& str);
	std::string		toUpper(const std::string& str);
	std::vector<std::string>	split(const std::string& str, char delimiter);
	std::vector<std::string>	split(const std::string& str, const std::string& delimiter);
	std::string		join(const std::vector<std::string>& vec, const std::string& delimiter);
	bool			startsWith(const std::string& str, const std::string& prefix);
	bool			endsWith(const std::string& str, const std::string& suffix);

	// Type conversion
	std::string		toString(int value);
	std::string		toString(size_t value);
	std::string		toString(long value);
	int				toInt(const std::string& str);
	size_t			toSize(const std::string& str);

	// File operations
	bool			fileExists(const std::string& path);
	bool			isDirectory(const std::string& path);
	bool			isFile(const std::string& path);
	bool			isReadable(const std::string& path);
	std::string		readFile(const std::string& path);
	bool			writeFile(const std::string& path, const std::string& content);
	std::string		getFileExtension(const std::string& path);
	std::string		getFileName(const std::string& path);
	std::string		getDirectory(const std::string& path);
	std::vector<std::string>	listDirectory(const std::string& path);

	// URL/Path operations
	std::string		urlDecode(const std::string& str);
	std::string		urlEncode(const std::string& str);
	std::string		normalizePath(const std::string& path);
	std::string		joinPath(const std::string& path1, const std::string& path2);
	std::string		resolvePath(const std::string& root, const std::string& path);

	// MIME types
	std::string		getMimeType(const std::string& extension);
	std::string		getDefaultMimeType();

	// Date/Time
	std::string		getHttpDate();
	std::string		getHttpDate(time_t time);

	// HTTP utilities
	std::string		getStatusMessage(int statusCode);
	bool			isValidMethod(const std::string& method);
	bool			isValidStatusCode(int code);

	// Encoding
	std::string		base64Encode(const std::string& input);
	std::string		base64Decode(const std::string& input);
	std::string		hexEncode(const std::string& input);
	std::string		hexDecode(const std::string& input);
}

#endif
