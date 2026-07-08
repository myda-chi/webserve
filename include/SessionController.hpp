#ifndef SESSIONCONTROLLER_HPP
# define SESSIONCONTROLLER_HPP

# include <string>
# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "ServerConfig.hpp"

// Bonus session surface: /session, /login, /logout, /my-uploads.
// Kept out of RequestHandler so the mandatory HTTP core contains no
// session-endpoint logic and the bonus stays removable.
class SessionController {
private:
	HttpRequest&	_request;
	HttpResponse&	_response;
	ServerConfig&	_config;

	// Handler methods
	void	handleSession();
	void	handleLogin();
	void	handleLogout();
	void	handleMyUploads();
	void	handleError(int statusCode);

public:
	// Orthodox Canonical Form
	SessionController(HttpRequest& request, HttpResponse& response, ServerConfig& config);
	SessionController(const SessionController& other);
	SessionController& operator=(const SessionController& other);
	~SessionController();

	// True when path+method is one of the session endpoints; callers must
	// then dispatch via handle() instead of the regular routing.
	static bool	handles(const std::string& path, const std::string& method);
	void		handle();
};

#endif
