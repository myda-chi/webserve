#include "../include/RequestHandler.hpp"
#include "../include/CgiHandler.hpp"
#include "../include/FileRegistry.hpp"
#include "../include/HttpUtils.hpp"
#include "../include/SessionController.hpp"

#include <dirent.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <sstream>

using HttpUtils::pathExists;
using HttpUtils::isDirectory;
using HttpUtils::isRegularFile;
using HttpUtils::urlDecode;
using HttpUtils::trim;
using HttpUtils::baseName;
using HttpUtils::ownerOf;
using HttpUtils::sanitizeFileName;
using HttpUtils::joinPath;

namespace {
	static std::string extensionOf(const std::string& path) {
		size_t dot = path.find_last_of('.');
		if (dot == std::string::npos)
			return "";
		return path.substr(dot + 1);
	}

	static std::string mimeFromExt(const std::string& ext) {
		if (ext == "html" || ext == "htm") return "text/html";
		if (ext == "css") return "text/css";
		if (ext == "js") return "application/javascript";
		if (ext == "json") return "application/json";
		if (ext == "txt") return "text/plain";
		if (ext == "png") return "image/png";
		if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
		if (ext == "gif") return "image/gif";
		if (ext == "ico") return "image/x-icon";
		return "application/octet-stream";
	}

	static std::string generatedUploadName() {
		std::ostringstream oss;
		oss << "upload_" << static_cast<long>(std::time(NULL)) << ".dat";
		return oss.str();
	}

	static std::string boundaryFromContentType(const std::string& contentType) {
		std::string marker = "boundary=";
		size_t pos = contentType.find(marker);
		if (pos == std::string::npos)
			return "";
		return trim(contentType.substr(pos + marker.size()));
	}

	static bool extractMultipartFile(const std::string& body, const std::string& boundary,
		std::string& filename, std::string& content) {
		if (boundary.empty())
			return false;
		std::string delimiter = "--" + boundary;
		size_t partStart = body.find(delimiter);
		while (partStart != std::string::npos) {
			partStart += delimiter.size();
			if (body.compare(partStart, 2, "--") == 0)
				return false;
			if (body.compare(partStart, 2, "\r\n") == 0)
				partStart += 2;
			else if (partStart < body.size() && body[partStart] == '\n')
				partStart += 1;

			size_t headerEnd = body.find("\r\n\r\n", partStart);
			size_t sepLength = 4;
			if (headerEnd == std::string::npos) {
				headerEnd = body.find("\n\n", partStart);
				sepLength = 2;
			}
			if (headerEnd == std::string::npos)
				return false;

			std::string headers = body.substr(partStart, headerEnd - partStart);
			size_t filenamePos = headers.find("filename=");
			if (filenamePos != std::string::npos) {
				filename = trim(headers.substr(filenamePos + 9));
				size_t stop = filename.find_first_of(";\r\n");
				if (stop != std::string::npos)
					filename = trim(filename.substr(0, stop));

				size_t contentStart = headerEnd + sepLength;
				size_t nextBoundary = body.find("\r\n" + delimiter, contentStart);
				if (nextBoundary == std::string::npos)
					nextBoundary = body.find("\n" + delimiter, contentStart);
				if (nextBoundary == std::string::npos)
					return false;
				content = body.substr(contentStart, nextBoundary - contentStart);
				return true;
			}
			partStart = body.find(delimiter, headerEnd + sepLength);
		}
		return false;
	}

	// Returns the index just past the first path segment whose extension is a
	// registered CGI extension for the route (the CGI script), so that the rest
	// of the path can be exposed to the CGI as PATH_INFO. npos when no segment
	// maps to a CGI script.
	static size_t findCgiSplit(const std::string& path, const Route& route) {
		size_t segStart = 0;
		while (segStart < path.size()) {
			size_t segEnd = path.find('/', segStart + 1);
			if (segEnd == std::string::npos)
				segEnd = path.size();
			size_t dot = path.rfind('.', segEnd - 1);
			if (dot != std::string::npos && dot > segStart) {
				std::string ext = path.substr(dot, segEnd - dot);
				if (route.hasCgiExtension(ext))
					return segEnd;
			}
			segStart = segEnd;
		}
		return std::string::npos;
	}
}

// Orthodox Canonical Form
RequestHandler::RequestHandler(HttpRequest& request, HttpResponse& response, ServerConfig& config) 
	: _request(request), _response(response), _config(config), _route(NULL) {
}

RequestHandler::RequestHandler(const RequestHandler& other)
	: _request(other._request), _response(other._response), _config(other._config) {
	_route = other._route;
}

RequestHandler& RequestHandler::operator=(const RequestHandler& other) {
	if (this != &other) {
		_route = other._route;
	}
	return *this;
}

RequestHandler::~RequestHandler() {
}

// Main handler
void RequestHandler::handle() {
	if (SessionController::handles(_request.getPath(), _request.getMethod())) {
		SessionController(_request, _response, _config).handle();
		return;
	}

	_route = _config.matchRoute(_request.getPath());

	if (_route != NULL && !_route->getRedirect().empty()) {
		handleRedirect(_route->getRedirect());
		return;
	}

	const std::string& method = _request.getMethod();
	if (method != "GET" && method != "POST" && method != "DELETE" && method != "HEAD") {
		handleError(501);
		return;
	}

	if (!isAllowedMethod(method)) {
		handleError(405);
		return;
	}

	if (_request.getBody().size() > _config.getClientMaxBodySize()) {
		handleError(413);
		return;
	}

	if (_request.getMethod() == "GET")
		handleGet();
	else if (_request.getMethod() == "HEAD")
		handleHead();
	else if (_request.getMethod() == "POST")
		handlePost();
	else if (_request.getMethod() == "DELETE")
		handleDelete();
	else
		handleError(405);
}

bool RequestHandler::startCgiIfNeeded(CgiHandler& cgi, bool& handled) {
	handled = false;
	_route = _config.matchRoute(_request.getPath());

	if (_route == NULL)
		return false;
	if (!_route->getRedirect().empty())
		return false;
	if (_request.getMethod() != "GET" && _request.getMethod() != "POST" && _request.getMethod() != "HEAD")
		return false;
	if (!isAllowedMethod(_request.getMethod()))
		return false;

	std::string reqPath = urlDecode(_request.getPath());
	size_t split = findCgiSplit(reqPath, *_route);
	if (split == std::string::npos)
		return false;

	std::string scriptUri = reqPath.substr(0, split);
	std::string pathInfo = reqPath.substr(split);
	std::string scriptPath = resolveDecodedPath(scriptUri);
	if (scriptPath.empty())
		return false;

	handled = true;
	if (!pathExists(scriptPath)) {
		handleError(404);
		return false;
	}

	std::string root = _config.getRoot();
	if (!_route->getRoot().empty())
		root = _route->getRoot();
	if (!root.empty() && root[root.size() - 1] == '/')
		root.erase(root.size() - 1);

	// The child inherits these through its environment, so they must be set
	// before start() forks.
	cgi.setServerPort(_config.getPort());
	cgi.setScriptName(scriptUri);
	cgi.setPathInfo(pathInfo, pathInfo.empty() ? "" : root + pathInfo);

	if (!cgi.start(scriptPath, *_route)) {
		if (_response.getStatusCode() == 200)
			handleError(502);
		return false;
	}
	return true;
}

// Private handler methods
void RequestHandler::handleGet() {
	std::string filePath = resolveFilePath();
	if (filePath.empty()) {
		handleError(403);
		return;
	}

	if (!pathExists(filePath)) {
		handleError(404);
		return;
	}

	if (isDirectory(filePath)) {
		if (_route != NULL && _route->getAutoindex()) {
			generateDirectoryListing(filePath);
			return;
		}
		handleError(403);
		return;
	}

	serveStaticFile(filePath);
}

void RequestHandler::handlePost() {
	std::string filePath = resolveFilePath();
	if (filePath.empty()) {
		handleError(403);
		return;
	}

	handleFileUpload();
}

void RequestHandler::handleDelete() {
	std::string filePath = resolveFilePath();

	if (filePath.empty()) {
		handleError(403);
		return;
	}
	if (!pathExists(filePath)) {
		handleError(404);
		return;
	}
	if (isDirectory(filePath)) {
		handleError(403);
		return;
	}
	if (!isRegularFile(filePath)) {
		handleError(403);
		return;
	}

	std::string diskName = baseName(filePath);
	std::string owner = ownerOf(diskName);
	Session* session = _request.getSession();
	std::string user = (session != NULL && session->hasKey("username"))
		? session->getValue("username") : "anonymous";
	if (owner != "anonymous" && owner != user) {
		handleError(403);
		return;
	}

	if (std::remove(filePath.c_str()) != 0) {
		handleError(403);
		return;
	}

	FileRegistry::getInstance().unregisterFile(owner, "/uploads/" + diskName);
	_response.setStatusCode(204);
	_response.setBody("");
}

void RequestHandler::handleHead() {
	// Same as GET; the body is dropped at build time (Client::prepareResponse)
	// so Content-Length still reflects the entity size, as RFC 7231 requires.
	handleGet();
}

// Private helper methods
void RequestHandler::serveStaticFile(const std::string& path) {
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		handleError(404);
		return;
	}

	std::ostringstream body;
	body << file.rdbuf();

	_response.setStatusCode(200);
	_response.setBody(body.str());
	_response.setContentType(mimeFromExt(extensionOf(path)));
}

void RequestHandler::generateDirectoryListing(const std::string& path) {
	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		handleError(403);
		return;
	}

	std::ostringstream html;
	html << "<html><body><h1>Index of " << _request.getPath() << "</h1><ul>";

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name(entry->d_name);
		if (name == ".")
			continue;
		std::string base = _request.getPath();
		if (!base.empty() && base[base.size() - 1] != '/')
    		base += '/';

		html << "<li><a href=\"" << base << name << "\">" << name << "</a></li>";
	}
	closedir(dir);

	html << "</ul></body></html>";
	_response.setStatusCode(200);
	_response.setContentType("text/html");
	_response.setBody(html.str());
}

void RequestHandler::handleRedirect(const std::string& location) {
	int code = (_route != NULL) ? _route->getRedirectCode() : 301;
	_response.setStatusCode(code);
	_response.setLocation(location);
	_response.setContentType("text/html");
	std::ostringstream oss;
	oss << "<html><body><h1>" << code << " " << _response.getStatusMessage() << "</h1></body></html>";
	_response.setBody(oss.str());
}

void RequestHandler::handleFileUpload() {
	if (_route == NULL || _route->getUploadPath().empty()) {
		handleError(403);
		return;
	}
	if (!pathExists(_route->getUploadPath()) || !isDirectory(_route->getUploadPath())) {
		handleError(500);
		return;
	}

	std::string content = _request.getBody();
	std::string filename;
	std::string contentType = _request.getHeader("content-type");
	std::string boundary = boundaryFromContentType(contentType);
	if (!boundary.empty())
		extractMultipartFile(_request.getBody(), boundary, filename, content);

	if (filename.empty()) {
		std::string requestName = baseName(urlDecode(_request.getPath()));
		if (requestName.empty() || requestName == "uploads")
			requestName = generatedUploadName();
		filename = requestName;
	}

	Session* session = _request.getSession();
	std::string owner = "anonymous";
	if (session != NULL && session->hasKey("username"))
		owner = session->getValue("username");

	std::string clean = sanitizeFileName(filename);
	std::string diskName = owner + "~" + clean;
	std::string destination = joinPath(_route->getUploadPath(), diskName);
	if (owner == "anonymous") {
		std::string stem = clean;
		std::string ext;
		size_t dot = clean.find_last_of('.');
		if (dot != std::string::npos) {
			stem = clean.substr(0, dot);
			ext = clean.substr(dot);
		}
		for (int i = 1; pathExists(destination); ++i) {
			std::ostringstream oss;
			oss << owner << "~" << stem << "_" << i << ext;
			diskName = oss.str();
			destination = joinPath(_route->getUploadPath(), diskName);
		}
	}

	std::ofstream file(destination.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		handleError(500);
		return;
	}
	file.write(content.c_str(), static_cast<std::streamsize>(content.size()));
	if (!file.good()) {
		handleError(500);
		return;
	}

	std::string url = "/uploads/" + diskName;
	FileRegistry::getInstance().registerFile(owner, url);
	if (owner != "anonymous") {
		_response.setStatusCode(303);
		_response.setLocation("/my-uploads");
		_response.setContentType("text/plain");
		_response.setBody("Uploaded\n");
	} else {
		_response.setStatusCode(201);
		_response.setLocation(url);
		FileRegistry::getInstance().registerFile(url);
		_response.setStatusCode(201);
		_response.setLocation(url);
		_response.setContentType("text/plain");
		_response.setBody("Created\n");
	}
}

std::string RequestHandler::resolveFilePath() {
	return resolveDecodedPath(urlDecode(_request.getPath()));
}

std::string RequestHandler::resolveDecodedPath(const std::string& decodedPath) {
	std::string root = _config.getRoot();
	if (_route != NULL && !_route->getRoot().empty())
		root = _route->getRoot();

	std::string reqPath = decodedPath;
	if (reqPath.empty())
		reqPath = "/";

	if (reqPath.find("..") != std::string::npos || reqPath.find('\0') != std::string::npos)
		return "";

	std::string relative = reqPath;
	if (_route != NULL && _route->getPath() != "/" && relative.find(_route->getPath()) == 0)
		relative = relative.substr(_route->getPath().size());
	if (relative.empty())
		relative = "/";

	std::string fullPath = root;
	if (!fullPath.empty() && fullPath[fullPath.size() - 1] == '/' && !relative.empty() && relative[0] == '/')
		fullPath += relative.substr(1);
	else if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/' && !relative.empty() && relative[0] != '/')
		fullPath += "/" + relative;
	else
		fullPath += relative;

	if (isDirectory(fullPath)) {
		if (_route != NULL) {
			const std::vector<std::string>& routeIndexes = _route->getIndex();
			for (size_t i = 0; i < routeIndexes.size(); ++i) {
				std::string candidate = fullPath;
				if (!candidate.empty() && candidate[candidate.size() - 1] != '/')
					candidate += "/";
				candidate += routeIndexes[i];
				if (pathExists(candidate))
					return candidate;
			}
		}

		const std::vector<std::string>& indexes = _config.getIndex();
		for (size_t i = 0; i < indexes.size(); ++i) {
			std::string candidate = fullPath;
			if (!candidate.empty() && candidate[candidate.size() - 1] != '/')
				candidate += "/";
			candidate += indexes[i];
			if (pathExists(candidate))
				return candidate;
		}
	}

	return fullPath;
}

bool RequestHandler::isAllowedMethod(const std::string& method) {
	if (_route == NULL)
		return method == "GET" || method == "POST" || method == "DELETE" || method == "HEAD";
	if (method == "HEAD")
		return _route->isMethodAllowed("GET");
	return _route->isMethodAllowed(method);
}

// Error handling
void RequestHandler::handleError(int statusCode) {
	_response.setStatusCode(statusCode);
	if (statusCode == 405 && _route != NULL) {
		const std::vector<std::string>& methods = _route->getAllowedMethods();
		if (!methods.empty()) {
			std::string allowed;
			for (size_t i = 0; i < methods.size(); ++i) {
				if (i > 0)
					allowed += ", ";
				allowed += methods[i];
			}
			_response.addHeader("Allow", allowed);
		}
	}
	sendErrorPage(statusCode);
}

void RequestHandler::sendErrorPage(int statusCode) {
	HttpUtils::sendErrorPage(_response, _config, statusCode);
}
