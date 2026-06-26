#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <map>
# include <vector>
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
	std::string							_rawRequest;

	// Parsing helper methods
	void		parseRequestLine(const std::string& line);
	void		parseHeaders(const std::string& headerSection);
	void		parseBody(const std::string& bodySection);
	void		parseUri();
	void		parseChunkedBody(const std::string& bodySection);

public:
	// Orthodox Canonical Form
	HttpRequest();
	HttpRequest(const HttpRequest& other);
	HttpRequest& operator=(const HttpRequest& other);
	~HttpRequest();

	// Parsing
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

	// Utility methods
	bool		hasHeader(const std::string& key) const;
	bool		keepAlive() const;
	std::string	getPath() const;
};

#endif
