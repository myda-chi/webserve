#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <netinet/in.h>
# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "ServerConfig.hpp"
# include "CgiHandler.hpp"
# include "iostream"

class Client {
private:
	int					_fd;
	struct sockaddr_in	_address;
	HttpRequest			_request;
	HttpResponse		_response;
	std::string			_readBuffer;
	std::string			_writeBuffer;
	bool				_keepAlive;
	time_t				_lastActivity;
	ServerConfig*		_serverConfig;
	CgiHandler*			_cgi;

	// Helper methods
	void		updateLastActivity();

public:
	// Orthodox Canonical Form
	Client();
	Client(int fd, const struct sockaddr_in& address);
	Client(const Client& other);
	Client& operator=(const Client& other);
	~Client();

	// Client operations
	ssize_t		read();
	ssize_t		write();
	void		processRequest();
	void		prepareResponse();
	void		processCgiInput();
	void		processCgiOutput();
	void		finishCgi();
	void		failCgiTimeout();
	bool		isReadComplete() const;
	bool		isWriteComplete() const;
	void		close();

	// Getters
	int					getFd() const;
	HttpRequest&		getRequest();
	HttpResponse&		getResponse();
	bool				getKeepAlive() const;
	bool				hasActiveCgi() const;
	bool				isCgiComplete();
	bool				isCgiTimeout(time_t currentTime, time_t timeout) const;
	int					getCgiInputFd() const;
	int					getCgiOutputFd() const;

	// Setters
	void		setFd(int fd);
	void		setServerConfig(ServerConfig* config);
	void		setKeepAlive(bool keepAlive);

	// Buffer operations
	void		clearReadBuffer();
	void		clearWriteBuffer();

	// Utility methods
	bool		isTimeout(time_t currentTime, time_t timeout) const;
};

#endif
