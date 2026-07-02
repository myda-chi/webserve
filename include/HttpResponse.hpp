#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

# include <string>
# include <map>
# include "CookieParser.hpp"

class HttpResponse {
	private:
	int									_statusCode;
	std::string							_statusMessage;
	std::string							_httpVersion;
	std::map<std::string, std::string>	_headers;
	std::string							_body;
	bool								_setCookie;
	std::string							_cookieHeader;

	// Helper methods
	std::string		getStatusMessage(int code) const;
	std::string		getHttpDate() const;
	std::string		buildHeaders();
	void			setDefaultHeaders();

public:
	// Orthodox Canonical Form
	HttpResponse();
	HttpResponse(const HttpResponse& other);
	HttpResponse& operator=(const HttpResponse& other);
	~HttpResponse();

	// Setters
	void		setStatusCode(int code);
	void		addHeader(const std::string& key, const std::string& value);
	void		setBody(const std::string& body);
	void		setCookieHeader(const std::string& cookie);

	// Getters
	int									getStatusCode() const;
	const std::string&					getStatusMessage() const;
	const std::string&					getBody() const;
	const std::string&					getCookieHeader() const;

	// Response building
	std::string		build();
	void			clear();

	// Utility methods
	void		setContentType(const std::string& type);
	void		setContentLength(size_t length);
	void		setLocation(const std::string& location);

	// Error responses
	void		buildErrorResponse(int code, const std::string& errorPage);
};

#endif
