#ifndef HTTPUTILS_HPP
# define HTTPUTILS_HPP

# include <string>
# include <map>

class HttpResponse;
class ServerConfig;

namespace HttpUtils {
	bool		pathExists(const std::string& path);
	bool		isDirectory(const std::string& path);
	bool		isRegularFile(const std::string& path);
	std::string	urlDecode(const std::string& value);
	std::string	trim(const std::string& value);
	std::string	baseName(const std::string& path);
	// Owner encoded in an upload's disk name ("alice~photo.jpg" -> "alice");
	// names without a '~' belong to "anonymous".
	std::string	ownerOf(const std::string& diskName);
	std::string	sanitizeFileName(const std::string& name);
	std::string	joinPath(const std::string& dir, const std::string& file);
	std::map<std::string, std::string>	parseFormBody(const std::string& body);
	bool		hasWhitespace(const std::string& value);
	// Serves the configured error_page for statusCode, falling back to a
	// generated HTML page. Status code must already be set on the response.
	void		sendErrorPage(HttpResponse& response, const ServerConfig& config, int statusCode);
}

#endif
