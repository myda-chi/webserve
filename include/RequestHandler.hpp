#ifndef REQUESTHANDLER_HPP
# define REQUESTHANDLER_HPP

# include <string>
# include <iostream>
# include <algorithm>
# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "ServerConfig.hpp"
# include "Route.hpp"
# include "SessionManager.hpp"

class CgiHandler;

class RequestHandler {
private:
	HttpRequest&		_request;
	HttpResponse&		_response;
	ServerConfig&		_config;
	Route*				_route;

	// Handler methods
	void		handleGet();
	void		handlePost();
	void		handleDelete();
	void		handleHead();

	// Helper methods
	void		serveStaticFile(const std::string& path);
	void		generateDirectoryListing(const std::string& path);
	void		handleRedirect(const std::string& location);
	void		handleFileUpload();
	std::string	resolveFilePath();
	std::string	resolveDecodedPath(const std::string& decodedPath);
	bool		isAllowedMethod(const std::string& method);

public:
	// Orthodox Canonical Form
	RequestHandler(HttpRequest& request, HttpResponse& response, ServerConfig& config);
	RequestHandler(const RequestHandler& other);
	RequestHandler& operator=(const RequestHandler& other);
	~RequestHandler();

	// Main handler
	void		handle();
	// Returns true when a CGI child was forked and the caller must drive its
	// pipes through the event loop. When it returns false, `handled` says
	// whether a response was already produced (CGI error, e.g. 404/502):
	// if handled is false the request is not a CGI one — call handle().
	bool		startCgiIfNeeded(CgiHandler& cgi, bool& handled);

	// Error handling
	void		handleError(int statusCode);
	void		sendErrorPage(int statusCode);
};

#endif
