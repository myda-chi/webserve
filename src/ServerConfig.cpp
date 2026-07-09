/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ServerConfig.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hmensah- <hmensah-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/24 20:01:47 by myda-chi          #+#    #+#             */
/*   Updated: 2026/07/09 19:08:15 by hmensah-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/ServerConfig.hpp"

#include <algorithm>

// Orthodox Canonical Form
ServerConfig::ServerConfig() : _host("0.0.0.0"), _port(8080), _clientMaxBodySize(0) {
}

ServerConfig::ServerConfig(const ServerConfig& other) {
	*this = other;
}

ServerConfig& ServerConfig::operator=(const ServerConfig& other) {
	if (this != &other) {
		_host = other._host;
		_port = other._port;
		_serverNames = other._serverNames;
		_errorPages = other._errorPages;
		_clientMaxBodySize = other._clientMaxBodySize;
		_routes = other._routes;
		_root = other._root;
		_index = other._index;
	}
	return *this;
}

ServerConfig::~ServerConfig() {
}

// Setters
void ServerConfig::setHost(const std::string& host) {
	_host = host;
}

void ServerConfig::setPort(int port) {
	_port = port;
}

void ServerConfig::addServerName(const std::string& name) {
	_serverNames.push_back(name);
}

void ServerConfig::setErrorPage(int code, const std::string& path) {
	_errorPages[code] = path;
}

void ServerConfig::setClientMaxBodySize(size_t size) {
	_clientMaxBodySize = size;
}

void ServerConfig::addRoute(const Route& route) {
	_routes.push_back(route);
}

void ServerConfig::setRoot(const std::string& root) {
	_root = root;
}

void ServerConfig::addIndexFile(const std::string& file) {
	_index.push_back(file);
}

// Getters
const std::string& ServerConfig::getHost() const {
	return _host;
}

int ServerConfig::getPort() const {
	return _port;
}

size_t ServerConfig::getClientMaxBodySize() const {
	return _clientMaxBodySize;
}

const std::string& ServerConfig::getRoot() const {
	return _root;
}

const std::vector<std::string>& ServerConfig::getIndex() const {
	return _index;
}

const std::vector<Route>& ServerConfig::getRoutes() const {
	return _routes;
}

// Utility methods
std::string ServerConfig::getErrorPage(int code) const {
	std::map<int, std::string>::const_iterator it = _errorPages.find(code);
	if (it == _errorPages.end())
		return "";
	return it->second;
}

Route* ServerConfig::matchRoute(const std::string& path) {
	Route* best = NULL;
	size_t bestLength = 0;

	for (size_t i = 0; i < _routes.size(); ++i) {
		if (_routes[i].matches(path)) {
			if (_routes[i].getPath().size() >= bestLength) {
				best = &_routes[i];
				bestLength = _routes[i].getPath().size();
			}
		}
	}
	return best;
}

Route* ServerConfig::matchCgiRoute(const std::string& path) {
	for (size_t i = 0; i < _routes.size(); ++i) {
		size_t segStart = 0;
		while (segStart < path.size()) {
			size_t segEnd = path.find('/', segStart + 1);
			if (segEnd == std::string::npos)
				segEnd = path.size();
			size_t dot = path.rfind('.', segEnd - 1);
			if (dot != std::string::npos && dot > segStart) {
				std::string ext = path.substr(dot, segEnd - dot);
				if (_routes[i].hasCgiExtension(ext))
					return &_routes[i];
			}
			segStart = segEnd;
		}
	}
	return NULL;
}


