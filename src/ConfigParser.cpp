#include "../include/ConfigParser.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

// Orthodox Canonical Form
ConfigParser::ConfigParser() : _currentLine(0) {
}

ConfigParser::ConfigParser(const std::string& configFile) : _configFile(configFile), _currentLine(0) {
}

ConfigParser::ConfigParser(const ConfigParser& other) {
	*this = other;
}

ConfigParser& ConfigParser::operator=(const ConfigParser& other) {
	if (this != &other) {
		_configFile = other._configFile;
		_serverConfigs = other._serverConfigs;
		_currentLine = other._currentLine;
		_currentLocationPath = other._currentLocationPath;
	}
	return *this;
}

ConfigParser::~ConfigParser() {
}

// Parsing
bool ConfigParser::parse() {
	if (_configFile.empty())
		_configFile = getDefaultConfigPath();

	std::ifstream file(_configFile.c_str());
	if (!file.is_open())
		return false;

	_serverConfigs.clear();
	_currentLine = 0;

	std::string line;
	while (std::getline(file, line)) {
		++_currentLine;
		line = trim(line);
		if (isEmpty(line) || isComment(line))
			continue;

		if (line == "server{" || line == "server {") {
			parseServerBlock(file);
			continue;
		}

		if (line == "server") {
			std::string nextLine;
			if (!std::getline(file, nextLine))
				return false;
			++_currentLine;
			nextLine = trim(nextLine);
			if (nextLine != "{")
				return false;
			parseServerBlock(file);
			continue;
		}

		return false;
	}

	try {
		validateConfig();
	} catch (const std::exception&) {
		return false;
	}
	return true;
}

// Private parsing helper methods
void ConfigParser::parseServerBlock(std::ifstream& file) {
	ServerConfig config;
	std::string line;

	while (std::getline(file, line)) {
		++_currentLine;
		line = trim(line);
		if (isEmpty(line) || isComment(line))
			continue;
		if (line == "}") {
			_serverConfigs.push_back(config);
			return;
		}

		if (line.compare(0, 8, "location") == 0) {
			size_t bracePos = line.find('{');
			if (bracePos == std::string::npos)
				throw std::runtime_error("Invalid location block");

			std::string path = trim(line.substr(8, bracePos - 8));
			if (path.empty())
				path = "/";
			_currentLocationPath = path;
			parseLocationBlock(file, config);
			continue;
		}

		parseDirective(line, config);
	}

	throw std::runtime_error("Unclosed server block");
}

void ConfigParser::parseLocationBlock(std::ifstream& file, ServerConfig& config) {
	Route route(_currentLocationPath);
	std::string line;

	while (std::getline(file, line)) {
		++_currentLine;
		line = trim(line);
		if (isEmpty(line) || isComment(line))
			continue;
		if (line == "}") {
			config.addRoute(route);
			return;
		}
		parseRouteDirective(line, route);
	}

	throw std::runtime_error("Unclosed location block");
}

void ConfigParser::parseDirective(const std::string& line, ServerConfig& config) {
	std::string clean = trim(line);
	if (!clean.empty() && clean[clean.size() - 1] == ';')
		clean = clean.substr(0, clean.size() - 1);

	std::vector<std::string> tokens = split(clean, ' ');
	if (tokens.empty())
		return;

	if (tokens[0] == "listen" && tokens.size() >= 2) {
		std::string value = tokens[1];
		size_t sep = value.find(':');
		if (sep == std::string::npos)
			config.setPort(std::atoi(value.c_str()));
		else {
			config.setHost(value.substr(0, sep));
			config.setPort(std::atoi(value.substr(sep + 1).c_str()));
		}
	} else if (tokens[0] == "server_name" && tokens.size() >= 2) {
		for (size_t i = 1; i < tokens.size(); ++i)
			config.addServerName(tokens[i]);
	} else if (tokens[0] == "root" && tokens.size() >= 2) {
		config.setRoot(tokens[1]);
	} else if (tokens[0] == "index" && tokens.size() >= 2) {
		for (size_t i = 1; i < tokens.size(); ++i)
			config.addIndexFile(tokens[i]);
	} else if (tokens[0] == "client_max_body_size" && tokens.size() >= 2) {
		config.setClientMaxBodySize(static_cast<size_t>(std::strtoul(tokens[1].c_str(), NULL, 10)));
	} else if (tokens[0] == "error_page" && tokens.size() >= 3) {
		std::string page = tokens[tokens.size() - 1];
		for (size_t i = 1; i + 1 < tokens.size(); ++i)
			config.setErrorPage(std::atoi(tokens[i].c_str()), page);
	}
}

void ConfigParser::parseRouteDirective(const std::string& line, Route& route) {
	std::string clean = trim(line);
	if (!clean.empty() && clean[clean.size() - 1] == ';')
		clean = clean.substr(0, clean.size() - 1);

	std::vector<std::string> tokens = split(clean, ' ');
	if (tokens.empty())
		return;

	if (tokens[0] == "allowed_methods" && tokens.size() >= 2) {
		for (size_t i = 1; i < tokens.size(); ++i)
			route.addAllowedMethod(tokens[i]);
	} else if (tokens[0] == "root" && tokens.size() >= 2) {
		route.setRoot(tokens[1]);
	} else if (tokens[0] == "autoindex" && tokens.size() >= 2) {
		route.setAutoindex(tokens[1] == "on");
	} else if (tokens[0] == "index" && tokens.size() >= 2) {
		for (size_t i = 1; i < tokens.size(); ++i)
			route.addIndexFile(tokens[i]);
	} else if (tokens[0] == "upload_path" && tokens.size() >= 2) {
		route.setUploadPath(tokens[1]);
	} else if (tokens[0] == "return" && tokens.size() >= 3) {
		int statusCode = std::atoi(tokens[1].c_str());
		if (statusCode >= 300 && statusCode <= 399)
			route.setRedirectCode(statusCode);
		route.setRedirect(tokens[2]);
	} else if (tokens[0] == "cgi_extension" && tokens.size() >= 3) {
		route.addCgiExtension(tokens[1], tokens[2]);
	} else if (tokens[0] == "client_max_body_size" && tokens.size() >= 2) {
		route.setClientMaxBodySize(static_cast<size_t>(std::strtoul(tokens[1].c_str(), NULL, 10)));
	}
}

std::string ConfigParser::trim(const std::string& str) {
	if (str.empty())
		return "";
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = str.find_last_not_of(" \t\r\n");
	std::string out = str.substr(start, end - start + 1);
	size_t hash = out.find('#');
	if (hash != std::string::npos)
		out = trim(out.substr(0, hash));
	return out;
}

bool ConfigParser::isComment(const std::string& line) {
	std::string clean = trim(line);
	return !clean.empty() && clean[0] == '#';
}

bool ConfigParser::isEmpty(const std::string& line) {
	return trim(line).empty();
}

// Validation
void ConfigParser::validateConfig() {
	if (_serverConfigs.empty())
		throw std::runtime_error("No server block found");
	for (size_t i = 0; i < _serverConfigs.size(); ++i)
		validateServerConfig(_serverConfigs[i]);
}

void ConfigParser::validateServerConfig(const ServerConfig& config) {
	if (config.getPort() <= 0 || config.getPort() > 65535)
		throw std::runtime_error("Invalid listen port");
}

// Getters
const std::vector<ServerConfig>& ConfigParser::getServerConfigs() const {
	return _serverConfigs;
}

// Static utility methods
std::vector<std::string> ConfigParser::split(const std::string& str, char delimiter) {
	std::vector<std::string> tokens;
	std::stringstream ss(str);
	std::string item;
	while (std::getline(ss, item, delimiter)) {
		item = item.empty() ? item : ConfigParser().trim(item);
		if (!item.empty())
			tokens.push_back(item);
	}
	return tokens;
}

std::string ConfigParser::getDefaultConfigPath() {
	return "config/default.conf";
}
