/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Socket.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: myda-chi <myda-chi@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/24 20:01:36 by myda-chi          #+#    #+#             */
/*   Updated: 2026/06/24 20:01:37 by myda-chi         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Socket.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

// Orthodox Canonical Form
Socket::Socket() : _fd(-1), _port(0), _host("0.0.0.0"), _isListening(false) {
	std::memset(&_address, 0, sizeof(_address));
}

Socket::Socket(const std::string& host, int port) : _fd(-1), _port(port), _host(host), _isListening(false) {
	std::memset(&_address, 0, sizeof(_address));
}

Socket::Socket(const Socket& other) {
	*this = other;
}

Socket& Socket::operator=(const Socket& other) {
	if (this != &other) {
		_fd = other._fd;
		_port = other._port;
		_host = other._host;
		_address = other._address;
		_isListening = other._isListening;
	}
	return *this;
}

Socket::~Socket() {
	if (_fd != -1)
		close();
}

// Socket operations
bool Socket::create() {
	_fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (_fd < 0)
		return false;

	setSocketOptions();
	setNonBlocking();

	std::memset(&_address, 0, sizeof(_address));
	_address.sin_family = AF_INET;
	_address.sin_port = htons(_port);
	if (_host.empty() || _host == "0.0.0.0")
		_address.sin_addr.s_addr = INADDR_ANY;
	else if (::inet_pton(AF_INET, _host.c_str(), &_address.sin_addr) != 1)
		return false;

	return true;
}

bool Socket::bind() {
	if (_fd < 0)
		return false;
	return ::bind(_fd, reinterpret_cast<struct sockaddr*>(&_address), sizeof(_address)) == 0;
}

bool Socket::listen(int backlog) {
	if (_fd < 0)
		return false;
	if (::listen(_fd, backlog) != 0)
		return false;
	_isListening = true;
	return true;
}

void Socket::close() {
	if (_fd != -1) {
		::close(_fd);
		_fd = -1;
	}
}

// Private helper methods
void Socket::setNonBlocking() {
	if (_fd < 0)
		return;
	fcntl(_fd, F_SETFL, O_NONBLOCK);
}

void Socket::setSocketOptions() {
	if (_fd < 0)
		return;
	int opt = 1;
	setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

// Getters
int Socket::getFd() const {
	return _fd;
}

int Socket::getPort() const {
	return _port;
}

const std::string& Socket::getHost() const {
	return _host;
}

// Setters
void Socket::setFd(int fd) {
	_fd = fd;
}
