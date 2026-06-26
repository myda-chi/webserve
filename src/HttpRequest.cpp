#include "../include/HttpRequest.hpp"

#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {
	static std::string toLowerCase(const std::string& value) {
		std::string out = value;
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = static_cast<char>(std::tolower(out[i]));
		return out;
	}

	static std::string trim(const std::string& value) {
		size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return "";
		size_t end = value.find_last_not_of(" \t\r\n");
		return value.substr(start, end - start + 1);
	}

	static bool parseHexSize(const std::string& line, size_t& size) {
		std::string clean = trim(line);
		size_t semicolon = clean.find(';');
		if (semicolon != std::string::npos)
			clean = clean.substr(0, semicolon);
		if (clean.empty())
			return false;
		for (size_t i = 0; i < clean.size(); ++i) {
			if (!std::isxdigit(static_cast<unsigned char>(clean[i])))
				return false;
		}
		size = static_cast<size_t>(std::strtoul(clean.c_str(), NULL, 16));
		return true;
	}

	static bool isToken(const std::string& value) {
		if (value.empty())
			return false;
		for (size_t i = 0; i < value.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(value[i]);
			if (!std::isalpha(c))
				return false;
		}
		return true;
	}

	static bool isDigits(const std::string& value) {
		if (value.empty())
			return false;
		for (size_t i = 0; i < value.size(); ++i) {
			if (!std::isdigit(static_cast<unsigned char>(value[i])))
				return false;
		}
		return true;
	}
}

// Orthodox Canonical Form
HttpRequest::HttpRequest() : _httpVersion("HTTP/1.1"), _isComplete(false), _isChunked(false), _isValid(true), _errorCode(0), _contentLength(0) {
}

HttpRequest::HttpRequest(const HttpRequest& other) {
	*this = other;
}

HttpRequest& HttpRequest::operator=(const HttpRequest& other) {
	if (this != &other) {
		_method = other._method;
		_uri = other._uri;
		_httpVersion = other._httpVersion;
		_headers = other._headers;
		_body = other._body;
		_queryString = other._queryString;
		_isComplete = other._isComplete;
		_isChunked = other._isChunked;
		_isValid = other._isValid;
		_errorCode = other._errorCode;
		_contentLength = other._contentLength;
		_rawRequest = other._rawRequest;
	}
	return *this;
}

HttpRequest::~HttpRequest() {
}

// Parsing
bool HttpRequest::parse(const std::string& rawRequest) {
	clear();
	_rawRequest = rawRequest;

	size_t headerEnd = rawRequest.find("\r\n\r\n");
	size_t sepLength = 4;
	if (headerEnd == std::string::npos) {
		headerEnd = rawRequest.find("\n\n");
		sepLength = 2;
	}
	if (headerEnd == std::string::npos)
		return true;

	std::string headerSection = rawRequest.substr(0, headerEnd);
	std::string bodySection = rawRequest.substr(headerEnd + sepLength);

	size_t lineEnd = headerSection.find("\r\n");
	size_t lineSep = 2;
	if (lineEnd == std::string::npos) {
		lineEnd = headerSection.find('\n');
		lineSep = 1;
	}
	std::string requestLine;
	std::string headers;
	if (lineEnd == std::string::npos) {
		requestLine = headerSection;
		headers = "";
	} else {
		requestLine = headerSection.substr(0, lineEnd);
		headers = headerSection.substr(lineEnd + lineSep);
	}

	parseRequestLine(requestLine);
	if (!_isValid) {
		_isComplete = true;
		return false;
	}
	parseHeaders(headers);
	if (!_isValid) {
		_isComplete = true;
		return false;
	}
	if (_httpVersion == "HTTP/1.1" && (!hasHeader("host") || getHeader("host").empty())) {
		_isValid = false;
		_errorCode = 400;
		_isComplete = true;
		return false;
	}

	if (_isChunked) {
		parseChunkedBody(bodySection);
		return true;
	}

	parseBody(bodySection);
	if (_contentLength == 0)
		_isComplete = true;
	else if (_body.size() >= _contentLength) {
		_body = _body.substr(0, _contentLength);
		_isComplete = true;
	}

	return true;
}

void HttpRequest::appendData(const std::string& data) {
	std::cout << "APPEND CALLED — current _isComplete: " << _isComplete 
	          << " — appending: [" << data << "]" << std::endl;
	_rawRequest.append(data);
	std::string rawRequest = _rawRequest;
	parse(rawRequest);
}

bool HttpRequest::isComplete() const {
	return _isComplete;
}

bool HttpRequest::isValid() const {
	return _isValid;
}

int HttpRequest::getErrorCode() const {
	return _errorCode;
}

void HttpRequest::clear() {
	_method.clear();
	_uri.clear();
	_httpVersion = "HTTP/1.1";
	_headers.clear();
	_body.clear();
	_queryString.clear();
	_isComplete = false;
	_isChunked = false;
	_isValid = true;
	_errorCode = 0;
	_contentLength = 0;
	_rawRequest.clear();
}

// Private parsing helpers
void HttpRequest::parseRequestLine(const std::string& line) {
	std::istringstream iss(line);
	std::string extra;
	if (!(iss >> _method >> _uri >> _httpVersion) || (iss >> extra)) {
		_isValid = false;
		_errorCode = 400;
		return;
	}
	if (!isToken(_method) || _uri.empty() || _uri[0] != '/' ||
		(_httpVersion != "HTTP/1.0" && _httpVersion != "HTTP/1.1")) {
		_isValid = false;
		_errorCode = 400;
		return;
	}
	parseUri();
}

void HttpRequest::parseHeaders(const std::string& headerSection) {
	std::istringstream iss(headerSection);
	std::string line;
	bool hasContentLength = false;
	bool hasTransferEncoding = false;

	while (std::getline(iss, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			continue;

		size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0) {
			_isValid = false;
			_errorCode = 400;
			continue;
		}

		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		if (key.find_first_of(" \t") != std::string::npos) {
			_isValid = false;
			_errorCode = 400;
			continue;
		}
		while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
			value.erase(0, 1);
		std::string lowerKey = toLowerCase(key);

		if (lowerKey == "host" && _headers.find("host") != _headers.end()) {
			_isValid = false;
			_errorCode = 400;
			continue;
		}

		if (lowerKey == "content-length") {
			if (hasContentLength || hasTransferEncoding) {
				_isValid = false;
				_errorCode = 400;
				continue;
			}
			hasContentLength = true;
		}

		if (lowerKey == "transfer-encoding") {
			if (hasContentLength) {
				_isValid = false;
				_errorCode = 400;
				continue;
			}
			hasTransferEncoding = true;
		}

		for (size_t i = 0; i < value.size(); ++i) {
			if (value[i] == '\r' || value[i] == '\n') {
				_isValid = false;
				_errorCode = 400;
				break;
			}
		}
		if (!_isValid)
			continue;

		_headers[lowerKey] = value;
	}

	if (!_isValid)
		return;
	if (hasHeader("content-length")) {
		std::string cl = getHeader("content-length");
		if (!isDigits(cl)) {
			_isValid = false;
			_errorCode = 400;
			return;
		}
		_contentLength = static_cast<size_t>(std::strtoul(cl.c_str(), NULL, 10));
		if (_contentLength > 0 && cl.find_first_not_of("0123456789") != std::string::npos) {
			_isValid = false;
			_errorCode = 400;
			return;
		}
	}
	if (hasHeader("transfer-encoding") && toLowerCase(getHeader("transfer-encoding")) == "chunked")
		_isChunked = true;
}

void HttpRequest::parseBody(const std::string& bodySection) {
	_body = bodySection;
}

void HttpRequest::parseUri() {
	size_t queryPos = _uri.find('?');
	if (queryPos == std::string::npos) {
		_queryString.clear();
		return;
	}
	_queryString = _uri.substr(queryPos + 1);
}

void HttpRequest::parseChunkedBody(const std::string& bodySection) {
	std::string decoded;
	size_t pos = 0;

	while (pos < bodySection.size()) {
		size_t lineEnd = bodySection.find("\r\n", pos);
		size_t sepLength = 2;
		if (lineEnd == std::string::npos) {
			lineEnd = bodySection.find('\n', pos);
			sepLength = 1;
		}
		if (lineEnd == std::string::npos)
			return;

		size_t chunkSize = 0;
		if (!parseHexSize(bodySection.substr(pos, lineEnd - pos), chunkSize)) {
			_isValid = false;
			_errorCode = 400;
			_isComplete = true;
			return;
		}
		pos = lineEnd + sepLength;

		if (chunkSize == 0) {
			_body = decoded;
			_contentLength = decoded.size();
			_isComplete = true;
			return;
		}

		if (bodySection.size() < pos + chunkSize)
			return;
		decoded.append(bodySection, pos, chunkSize);
		pos += chunkSize;

		if (bodySection.compare(pos, 2, "\r\n") == 0)
			pos += 2;
		else if (pos < bodySection.size() && bodySection[pos] == '\n')
			pos += 1;
		else {
			_isValid = false;
			_errorCode = 400;
			_isComplete = true;
			return;
		}
	}
}

// Getters
const std::string& HttpRequest::getMethod() const {
	return _method;
}

const std::string& HttpRequest::getUri() const {
	return _uri;
}

const std::string& HttpRequest::getHttpVersion() const {
	return _httpVersion;
}

const std::map<std::string, std::string>& HttpRequest::getHeaders() const {
	return _headers;
}

std::string HttpRequest::getHeader(const std::string& key) const {
	std::map<std::string, std::string>::const_iterator it = _headers.find(toLowerCase(key));
	if (it == _headers.end())
		return "";
	return it->second;
}

const std::string& HttpRequest::getBody() const {
	return _body;
}

const std::string& HttpRequest::getQueryString() const {
	return _queryString;
}

// Utility methods
bool HttpRequest::hasHeader(const std::string& key) const {
	return _headers.find(toLowerCase(key)) != _headers.end();
}

bool HttpRequest::keepAlive() const {
	std::string connection = toLowerCase(getHeader("connection"));
	if (_httpVersion == "HTTP/1.1")
		return connection != "close";
	return connection == "keep-alive";
}

std::string HttpRequest::getPath() const {
	size_t queryPos = _uri.find('?');
	if (queryPos == std::string::npos)
		return _uri.empty() ? "/" : _uri;
	return _uri.substr(0, queryPos);
}
