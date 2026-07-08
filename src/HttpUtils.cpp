#include "../include/HttpUtils.hpp"
#include "../include/HttpResponse.hpp"
#include "../include/ServerConfig.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace {
	static int fromHex(char c) {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}
}

namespace HttpUtils {

bool pathExists(const std::string& path) {
	struct stat st;
	return ::stat(path.c_str(), &st) == 0;
}

bool isDirectory(const std::string& path) {
	struct stat st;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
}

bool isRegularFile(const std::string& path) {
	struct stat st;
	if (::stat(path.c_str(), &st) != 0)
		return false;
	return S_ISREG(st.st_mode);
}

std::string urlDecode(const std::string& value) {
	std::string out;
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '%' && i + 2 < value.size()) {
			int hi = fromHex(value[i + 1]);
			int lo = fromHex(value[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out += static_cast<char>(hi * 16 + lo);
				i += 2;
				continue;
			}
		} else if (value[i] == '+') {
			out += ' ';
			continue;
		}
		out += value[i];
	}
	return out;
}

std::string trim(const std::string& value) {
	size_t start = value.find_first_not_of(" \t\r\n\"");
	if (start == std::string::npos)
		return "";
	size_t end = value.find_last_not_of(" \t\r\n\"");
	return value.substr(start, end - start + 1);
}

std::string baseName(const std::string& path) {
	size_t slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}

std::string ownerOf(const std::string& diskName) {
	size_t tilde = diskName.find('~');
	return tilde == std::string::npos ? "anonymous" : diskName.substr(0, tilde);
}

std::string sanitizeFileName(const std::string& name) {
	std::string clean = baseName(name);
	std::string out;
	for (size_t i = 0; i < clean.size(); ++i) {
		char c = clean[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
			out += c;
	}
	return out.empty() ? "upload.dat" : out;
}

std::string joinPath(const std::string& dir, const std::string& file) {
	if (dir.empty())
		return file;
	if (dir[dir.size() - 1] == '/')
		return dir + file;
	return dir + "/" + file;
}

std::map<std::string, std::string> parseFormBody(const std::string& body) {
	std::map<std::string, std::string> fields;
	std::istringstream stream(body);
	std::string token;
	while (std::getline(stream, token, '&')) {
		size_t eq = token.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = urlDecode(token.substr(0, eq));
		std::string value = urlDecode(token.substr(eq + 1));
		fields[key] = value;
	}
	return fields;
}

bool hasWhitespace(const std::string& value) {
	for (size_t i = 0; i < value.length(); i++) {
		if (std::isspace(static_cast<unsigned char>(value[i])))
			return true;
	}
	return false;
}

void sendErrorPage(HttpResponse& response, const ServerConfig& config, int statusCode) {
	std::string errorPath = config.getErrorPage(statusCode);
	if (!errorPath.empty()) {
		std::ifstream file(errorPath.c_str(), std::ios::in | std::ios::binary);
		if (!file.is_open() && errorPath[0] == '/')
			file.open(("." + errorPath).c_str(), std::ios::in | std::ios::binary);
		if (!file.is_open() && errorPath[0] == '/')
			file.open(("./www" + errorPath).c_str(), std::ios::in | std::ios::binary);
		if (!file.is_open() && errorPath[0] != '/')
			file.open((joinPath(config.getRoot(), errorPath)).c_str(), std::ios::in | std::ios::binary);
		if (file.is_open()) {
			std::ostringstream body;
			body << file.rdbuf();
			response.setContentType("text/html");
			response.setBody(body.str());
			return;
		}
	}

	std::ostringstream fallback;
	fallback << "<html><body><h1>" << statusCode << " " << response.getStatusMessage() << "</h1></body></html>";
	response.setContentType("text/html");
	response.setBody(fallback.str());
}

}
