#include "../include/SessionController.hpp"
#include "../include/FileRegistry.hpp"
#include "../include/HttpUtils.hpp"

#include <sstream>

using HttpUtils::trim;
using HttpUtils::baseName;
using HttpUtils::hasWhitespace;
using HttpUtils::parseFormBody;
using HttpUtils::sanitizeFileName;

// Orthodox Canonical Form
SessionController::SessionController(HttpRequest& request, HttpResponse& response, ServerConfig& config)
	: _request(request), _response(response), _config(config) {
}

SessionController::SessionController(const SessionController& other)
	: _request(other._request), _response(other._response), _config(other._config) {
}

SessionController& SessionController::operator=(const SessionController& other) {
	(void)other;
	return *this;
}

SessionController::~SessionController() {
}

// Main handler
bool SessionController::handles(const std::string& path, const std::string& method) {
	if (method == "GET")
		return path == "/session" || path == "/my-uploads";
	if (method == "POST")
		return path == "/login" || path == "/logout";
	return false;
}

void SessionController::handle() {
	if (_request.getPath() == "/session")
		handleSession();
	else if (_request.getPath() == "/login")
		handleLogin();
	else if (_request.getPath() == "/logout")
		handleLogout();
	else if (_request.getPath() == "/my-uploads")
		handleMyUploads();
}

// Private handler methods
void SessionController::handleSession() {
	Session* session = _request.getSession();

	_response.setStatusCode(200);
	_response.setContentType("text/plain");

	if (session != NULL && session->hasKey("username"))
		_response.setBody("Logged in as " + session->getValue("username") + "\n");
	else
		_response.setBody("Not logged in\n");
}

void SessionController::handleLogin() {
	std::string contentType = _request.getHeader("content-type");
	if (contentType.find("application/x-www-form-urlencoded") == std::string::npos) {
		handleError(415);
		return;
	}

	std::map<std::string, std::string> fields = parseFormBody(_request.getBody());
	std::map<std::string, std::string>::iterator it = fields.find("username");
	if (it == fields.end()) {
		_response.setStatusCode(303);
		_response.setLocation("/");
		_response.setContentType("text/plain");
		_response.setBody("Failed to login\n");
		return;
	}

	std::string username = trim(it->second);
	if (username.empty() || hasWhitespace(username) ||
		username == "anonymous" || sanitizeFileName(username) != username) {
		_response.setStatusCode(303);
		_response.setLocation("/");
		_response.setContentType("text/plain");
		_response.setBody("username not valid\n");
		return;
	}

	Session* session = _request.getSession();
	if (session == NULL) {
		handleError(500);
		return;
	}

	session->setData("username", username);

	_response.setStatusCode(303);
	_response.setLocation("/");
	_response.setContentType("text/plain");
	_response.setBody("Successfully logged in\n");
}

void SessionController::handleLogout() {
	Session* session = _request.getSession();
	if (session != NULL) {
		if (session->hasKey("username"))
			session->unsetData("username");
		else {
			_response.setStatusCode(303);
			_response.setLocation("/");
			_response.setContentType("text/plain");
			_response.setBody("Not logged in\n");
			return;
		}
	}

	_response.setStatusCode(303);
	_response.setLocation("/");
	_response.setContentType("text/plain");
	_response.setBody("Successfully logged out\n");
}

void SessionController::handleMyUploads() {
	std::vector<std::string> files;
	Session* session = _request.getSession();
	if (session == NULL || !session->hasKey("username")) {
		files = FileRegistry::getInstance().getFiles("anonymous");
	}
	else {
		std::string username = session->getValue("username");
		files = FileRegistry::getInstance().getFiles(username);
	}

	std::ostringstream html;
	html << "<html><body><h1>My Uploads</h1><ul>";
	for (size_t i = 0; i < files.size(); ++i) {
		std::string fileName = baseName(files[i]);
		html << "<li><a href=\"" << files[i] << "\">" << fileName << "</a></li>";
	}
	html << "</ul></body></html>";

	_response.setStatusCode(200);
	_response.setContentType("text/html");
	_response.setBody(html.str());
}

// Error handling
void SessionController::handleError(int statusCode) {
	_response.setStatusCode(statusCode);
	HttpUtils::sendErrorPage(_response, _config, statusCode);
}
