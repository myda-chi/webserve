/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: zsalih <zsalih@student.42abudhabi.ae>      +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/08 22:21:34 by hmensah-          #+#    #+#             */
/*   Updated: 2026/06/20 22:18:39 by zsalih           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/HttpResponse.hpp"

#include <sstream>
#include <ctime>

// Orthodox Canonical Form
HttpResponse::HttpResponse() : _statusCode(200), _statusMessage("OK"), _httpVersion("HTTP/1.1"), _setCookie(false), _cookieHeader("") {
	setDefaultHeaders();
}

HttpResponse::HttpResponse(const HttpResponse& other) {
	*this = other;
}

HttpResponse& HttpResponse::operator=(const HttpResponse& other) {
	if (this != &other) {
		_statusCode = other._statusCode;
		_statusMessage = other._statusMessage;
		_httpVersion = other._httpVersion;
		_headers = other._headers;
		_body = other._body;
		_setCookie = other._setCookie;
		_cookieHeader = other._cookieHeader;
	}
	return *this;
}

HttpResponse::~HttpResponse() {
}

// Setters
void HttpResponse::setStatusCode(int code) {
	_statusCode = code;
	_statusMessage = getStatusMessage(code);
}

void HttpResponse::addHeader(const std::string& key, const std::string& value) {
	_headers[key] = value;
}

void HttpResponse::setBody(const std::string& body) {
	_body = body;
}

void HttpResponse::setCookieHeader(const std::string& cookie) {
	_setCookie = true;
	_cookieHeader = cookie;
	addHeader("Set-Cookie", cookie);
}

// Getters
int HttpResponse::getStatusCode() const {
	return _statusCode;
}

const std::string& HttpResponse::getStatusMessage() const {
	return _statusMessage;
}

const std::string& HttpResponse::getBody() const {
	return _body;
}

const std::string& HttpResponse::getCookieHeader() const {
	return _cookieHeader;
}

// Response building
std::string HttpResponse::build() {
	setContentLength(_body.size());
	addHeader("Date", getHttpDate());
	return buildHeaders() + _body;
}

std::string HttpResponse::buildHeaders() {
	std::ostringstream oss;
	oss << _httpVersion << " " << _statusCode << " " << _statusMessage << "\r\n";
	for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it)
		oss << it->first << ": " << it->second << "\r\n";
	oss << "\r\n";
	return oss.str();
}

void HttpResponse::clear() {
	_statusCode = 200;
	_statusMessage = getStatusMessage(200);
	_httpVersion = "HTTP/1.1";
	_headers.clear();
	_body.clear();
	_setCookie = false;
	_cookieHeader.clear();
	setDefaultHeaders();
}

// Utility methods
void HttpResponse::setContentType(const std::string& type) {
	addHeader("Content-Type", type);
}

void HttpResponse::setContentLength(size_t length) {
	std::ostringstream oss;
	oss << length;
	addHeader("Content-Length", oss.str());
}

void HttpResponse::setLocation(const std::string& location) {
	addHeader("Location", location);
}

void HttpResponse::buildErrorResponse(int code, const std::string& errorPage) {
	setStatusCode(code);
	setContentType("text/html");
	if (!errorPage.empty()) {
		setBody(errorPage);
		return;
	}
	std::ostringstream oss;
	oss << "<html><body><h1>" << code << " " << getStatusMessage(code) << "</h1></body></html>";
	setBody(oss.str());
}

// Private helper methods
std::string HttpResponse::getStatusMessage(int code) const {
	switch (code) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 307: return "Temporary Redirect";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 409: return "Conflict";
		case 413: return "Payload Too Large";
		case 500: return "Internal Server Error";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		case 501: return "Not Implemented";
		default: return "Unknown";
	}
}

void HttpResponse::setDefaultHeaders() {
	addHeader("Server", "webserv/0.1");
	addHeader("Connection", "close");
}

std::string HttpResponse::getHttpDate() const {
	time_t now = std::time(NULL);
	struct tm* gmt = std::gmtime(&now);
	char buf[64];
	std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
	return std::string(buf);
}
