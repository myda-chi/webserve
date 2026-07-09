#ifndef SERVERCONFIG_HPP
# define SERVERCONFIG_HPP

# include <string>
# include <vector>
# include <map>
# include "Route.hpp"

class ServerConfig {
private:
	std::string							_host;
	int									_port;
	std::vector<std::string>			_serverNames;
	std::map<int, std::string>			_errorPages;
	size_t								_clientMaxBodySize;
	std::vector<Route>					_routes;
	std::string							_root;
	std::vector<std::string>			_index;

public:
	// Orthodox Canonical Form
	ServerConfig();
	ServerConfig(const ServerConfig& other);
	ServerConfig& operator=(const ServerConfig& other);
	~ServerConfig();

	// Setters
	void		setHost(const std::string& host);
	void		setPort(int port);
	void		addServerName(const std::string& name);
	void		setErrorPage(int code, const std::string& path);
	void		setClientMaxBodySize(size_t size);
	void		addRoute(const Route& route);
	void		setRoot(const std::string& root);
	void		addIndexFile(const std::string& file);

	// Getters
	const std::string&					getHost() const;
	int									getPort() const;
	size_t								getClientMaxBodySize() const;
	const std::string&					getRoot() const;
	const std::vector<std::string>&		getIndex() const;
	const std::vector<Route>&			getRoutes() const;

	// Utility methods
	std::string		getErrorPage(int code) const;
	// Longest-prefix match over the configured location blocks ("/uploads"
	// beats "/" for /uploads/x). NULL when no route matches.
	Route*			matchRoute(const std::string& path);
	Route*			matchCgiRoute(const std::string& path);
};

#endif
