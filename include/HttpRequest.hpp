#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <map>
# include <vector>
# include "SessionManager.hpp"
# include <iostream>

class HttpRequest {
private:
	std::string							_method;
	std::string							_uri;
	std::string							_httpVersion;
	std::map<std::string, std::string>	_headers;
	std::string							_body;
	std::string							_queryString;
	bool								_isComplete;
	bool								_isChunked;
	bool								_isValid;
	int									_errorCode;
	size_t								_contentLength;
	size_t								_maxBodySize;
	std::string							_rawRequest;
	bool								_headersParsed;
	Session*							_session;
	std::string							_sessionId;

	// Parsing helper methods
	void		parseRequestLine(const std::string& line);
	bool		parseHeaderSection(const std::string& headerSection);
	void		finalizeBodyIfComplete();
	void		parseHeaders(const std::string& headerSection);
	void		parseBody(const std::string& bodySection);
	void		parseUri();
	void		parseChunkedBody(const std::string& bodySection);
	// Marks the request invalid AND complete (so the server stops reading and
	// responds with getErrorCode()); always returns false.
	bool		setError(int code);

public:
	// Orthodox Canonical Form
	HttpRequest();
	HttpRequest(const HttpRequest& other);
	HttpRequest& operator=(const HttpRequest& other);
	~HttpRequest();

	// Parsing.
	// appendData() accumulates raw bytes and re-parses; poll the result with
	// isComplete(). A complete request may still be invalid — check isValid()
	// and respond with getErrorCode() (400/413/414/431/505) before using it.
	bool		parse(const std::string& rawRequest);
	void		appendData(const std::string& data);
	bool		isComplete() const;
	bool		isValid() const;
	int			getErrorCode() const;
	void		clear();

	// Getters
	const std::string&					getMethod() const;
	const std::string&					getUri() const;
	const std::string&					getHttpVersion() const;
	const std::map<std::string, std::string>&	getHeaders() const;
	std::string							getHeader(const std::string& key) const;
	const std::string&					getBody() const;
	const std::string&					getQueryString() const;
	Session*							getSession() const;
	const std::string&					getSessionId() const;

	// Setters
	void	setSession(Session* session);
	void    setSessionId(const std::string& id);
	// Body-size limit from the server config (0 = unlimited). Enforced while
	// parsing so oversized bodies are rejected with 413 before being buffered.
	// Survives clear(), so it is set once per connection.
	void	setMaxBodySize(size_t maxBodySize);

	// Utility methods
	bool		hasHeader(const std::string& key) const;
	bool		keepAlive() const;
	std::string	getPath() const;
};

#endif
