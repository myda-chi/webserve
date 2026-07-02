#include "../include/Client.hpp"
#include "../include/RequestHandler.hpp"
#include "../include/SessionMiddleware.hpp"
#include <unistd.h>
#include <ctime>


// Orthodox Canonical Form
Client::Client() : _fd(-1), _keepAlive(true), _lastActivity(std::time(NULL)), _serverConfig(NULL), _cgi(NULL) {
}

Client::Client(int fd, const struct sockaddr_in& address) : _fd(fd), _address(address), _keepAlive(true), _lastActivity(std::time(NULL)), _serverConfig(NULL), _cgi(NULL) {
}

Client::Client(const Client& other) {
	*this = other;
}

Client& Client::operator=(const Client& other) {
	if (this != &other) {
		_fd = other._fd;
		_address = other._address;
		_request = other._request;
		_response = other._response;
		_readBuffer = other._readBuffer;
		_writeBuffer = other._writeBuffer;
		_keepAlive = other._keepAlive;
		_lastActivity = other._lastActivity;
		_serverConfig = other._serverConfig;
		_cgi = NULL;
	}
	return *this;
}

Client::~Client() {
	if (_cgi != NULL) {
		_cgi->killProcess();
		delete _cgi;
		_cgi = NULL;
	}
}

// Client operations
ssize_t Client::read() {
	char buffer[8192];
	ssize_t bytes = ::recv(_fd, buffer, sizeof(buffer), 0);
	if (bytes > 0) {
		_readBuffer.append(buffer, static_cast<size_t>(bytes));
		_request.appendData(std::string(buffer, static_cast<size_t>(bytes)));
		updateLastActivity();
		return bytes;
	}
	if (bytes == 0)
		return 0;
	return -1;
}

ssize_t Client::write() {
	if (_writeBuffer.empty())
		return 0;

	ssize_t bytes = ::send(_fd, _writeBuffer.c_str(), _writeBuffer.size(), MSG_NOSIGNAL);
	if (bytes > 0) {
		_writeBuffer.erase(0, static_cast<size_t>(bytes));
		updateLastActivity();
		return bytes;
	}
	return -1;
}

void Client::processRequest() {
	if (_serverConfig == NULL)
		return;

	_response.clear();
	if (_cgi != NULL) {
		_cgi->killProcess();
		delete _cgi;
		_cgi = NULL;
	}

	if (!_request.isValid()) {
		_response.buildErrorResponse(_request.getErrorCode(), "");
		setKeepAlive(false);
		_response.addHeader("Connection", "close");
		return;
	}

	SessionMiddleware sessionMiddleware;
	sessionMiddleware.processRequest(_request);

	RequestHandler handler(_request, _response, *_serverConfig);
	CgiHandler* cgi = new CgiHandler(_request, _response);
	bool handled = false;
	if (handler.startCgiIfNeeded(*cgi, handled)) {
		_cgi = cgi;
		updateLastActivity();
		return;
	}
	delete cgi;

	if (!handled)
		handler.handle();

	sessionMiddleware.processResponse(_request, _response);
	setKeepAlive(_request.keepAlive());
	if (_keepAlive)
		_response.addHeader("Connection", "keep-alive");
	else
		_response.addHeader("Connection", "close");
}

void Client::prepareResponse() {
	_writeBuffer = _response.build();
}

void Client::processCgiInput() {
	if (_cgi == NULL)
		return;
	_cgi->writeInput();
	updateLastActivity();
}

void Client::processCgiOutput() {
	if (_cgi == NULL)
		return;
	_cgi->readOutput();
	updateLastActivity();
}

void Client::finishCgi() {
	if (_cgi == NULL)
		return;
	_cgi->finish();
	delete _cgi;
	_cgi = NULL;
	SessionMiddleware sessionMiddleware;
	sessionMiddleware.processResponse(_request, _response);
	setKeepAlive(_request.keepAlive());
	if (_keepAlive)
		_response.addHeader("Connection", "keep-alive");
	else
		_response.addHeader("Connection", "close");
}

void Client::failCgiTimeout() {
	if (_cgi == NULL)
		return;
	_cgi->killProcess();
	_cgi->setGatewayError(504);
	delete _cgi;
	_cgi = NULL;
	setKeepAlive(false);
	_response.addHeader("Connection", "close");
}

bool Client::isReadComplete() const {
	return _request.isComplete();
}

bool Client::isWriteComplete() const {
	return _writeBuffer.empty();
}

void Client::close() {
	if (_cgi != NULL) {
		_cgi->killProcess();
		delete _cgi;
		_cgi = NULL;
	}
	if (_fd != -1) {
		::close(_fd);
		_fd = -1;
	}
}

// Private helper methods
void Client::updateLastActivity() {
	_lastActivity = std::time(NULL);
}

// Getters
int Client::getFd() const {
	return _fd;
}

HttpRequest& Client::getRequest() {
	return _request;
}

HttpResponse& Client::getResponse() {
	return _response;
}

bool Client::getKeepAlive() const {
	return _keepAlive;
}

bool Client::hasActiveCgi() const {
	return _cgi != NULL;
}

bool Client::isCgiComplete() {
	return _cgi != NULL && _cgi->isComplete();
}

bool Client::isCgiTimeout(time_t currentTime, time_t timeout) const {
	return _cgi != NULL && _cgi->isTimeout(currentTime, timeout);
}

int Client::getCgiInputFd() const {
	if (_cgi == NULL || !_cgi->wantsInputWrite())
		return -1;
	return _cgi->getInputFd();
}

int Client::getCgiOutputFd() const {
	if (_cgi == NULL || !_cgi->wantsOutputRead())
		return -1;
	return _cgi->getOutputFd();
}

// Setters
void Client::setFd(int fd) {
	_fd = fd;
}

void Client::setServerConfig(ServerConfig* config) {
	_serverConfig = config;
}

void Client::setKeepAlive(bool keepAlive) {
	_keepAlive = keepAlive;
}

// Buffer operations
void Client::clearReadBuffer() {
	_readBuffer.clear();
}

void Client::clearWriteBuffer() {
	_writeBuffer.clear();
}

// Utility methods
bool Client::isTimeout(time_t currentTime, time_t timeout) const {
	return (currentTime - _lastActivity) > timeout;
}
