#include "../include/Route.hpp"

#include <algorithm>

// Orthodox Canonical Form
Route::Route() : _path("/"), _autoindex(false), _redirectCode(301), _clientMaxBodySize(0), _hasClientMaxBodySize(false) {
}

Route::Route(const std::string& path) : _path(path), _autoindex(false), _redirectCode(301), _clientMaxBodySize(0), _hasClientMaxBodySize(false) {
}

Route::Route(const Route& other) {
	*this = other;
}

Route& Route::operator=(const Route& other) {
	if (this != &other) {
		_path = other._path;
		_allowedMethods = other._allowedMethods;
		_root = other._root;
		_autoindex = other._autoindex;
		_index = other._index;
		_redirect = other._redirect;
		_redirectCode = other._redirectCode;
		_uploadPath = other._uploadPath;
		_cgiExtensions = other._cgiExtensions;
		_clientMaxBodySize = other._clientMaxBodySize;
		_hasClientMaxBodySize = other._hasClientMaxBodySize;
	}
	return *this;
}

Route::~Route() {
}

// Setters
void Route::addAllowedMethod(const std::string& method) {
	_allowedMethods.push_back(method);
}

void Route::setRoot(const std::string& root) {
	_root = root;
}

void Route::setAutoindex(bool autoindex) {
	_autoindex = autoindex;
}

void Route::addIndexFile(const std::string& file) {
	_index.push_back(file);
}

void Route::setRedirect(const std::string& redirect) {
	_redirect = redirect;
}

void Route::setRedirectCode(int code) {
	_redirectCode = code;
}

void Route::setUploadPath(const std::string& path) {
	_uploadPath = path;
}

void Route::addCgiExtension(const std::string& ext, const std::string& handler) {
	_cgiExtensions[ext] = handler;
}

void Route::setClientMaxBodySize(size_t size) {
	_clientMaxBodySize = size;
	_hasClientMaxBodySize = true;
}

// Getters
const std::string& Route::getPath() const {
	return _path;
}

const std::vector<std::string>& Route::getAllowedMethods() const {
	return _allowedMethods;
}

const std::string& Route::getRoot() const {
	return _root;
}

bool Route::getAutoindex() const {
	return _autoindex;
}

const std::vector<std::string>& Route::getIndex() const {
	return _index;
}

const std::string& Route::getRedirect() const {
	return _redirect;
}

int Route::getRedirectCode() const {
	return _redirectCode;
}

const std::string& Route::getUploadPath() const {
	return _uploadPath;
}

bool Route::hasClientMaxBodySize() const {
	return _hasClientMaxBodySize;
}

size_t Route::getClientMaxBodySize() const {
	return _clientMaxBodySize;
}

// Utility methods
bool Route::isMethodAllowed(const std::string& method) const {
	if (_allowedMethods.empty())
		return true;
	return std::find(_allowedMethods.begin(), _allowedMethods.end(), method) != _allowedMethods.end();
}

bool Route::hasCgiExtension(const std::string& ext) const {
	return _cgiExtensions.find(ext) != _cgiExtensions.end();
}

std::string Route::getCgiHandler(const std::string& ext) const {
	std::map<std::string, std::string>::const_iterator it = _cgiExtensions.find(ext);
	if (it == _cgiExtensions.end())
		return "";
	return it->second;
}

bool Route::matches(const std::string& requestPath) const {
	if (_path == "/")
		return true;
	if (requestPath.size() < _path.size())
		return false;
	if (requestPath.compare(0, _path.size(), _path) != 0)
		return false;
	if (requestPath.size() == _path.size())
		return true;
	return requestPath[_path.size()] == '/';
}
