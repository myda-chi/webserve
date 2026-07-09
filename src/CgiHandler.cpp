#include "../include/CgiHandler.hpp"
#include "../include/Logger.hpp"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>

namespace {
	static std::string toString(size_t value) {
		std::ostringstream oss;
		oss << value;
		return oss.str();
	}

	static std::string trim(const std::string& value) {
		size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return "";
		size_t end = value.find_last_not_of(" \t\r\n");
		return value.substr(start, end - start + 1);
	}

	static std::string extensionWithDot(const std::string& path) {
		size_t slash = path.find_last_of('/');
		size_t dot = path.find_last_of('.');
		if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
			return "";
		return path.substr(dot);
	}

	static std::string directoryOf(const std::string& path) {
		size_t slash = path.find_last_of('/');
		if (slash == std::string::npos)
			return ".";
		if (slash == 0)
			return "/";
		return path.substr(0, slash);
	}

	static std::string fileNameOf(const std::string& path) {
		size_t slash = path.find_last_of('/');
		if (slash == std::string::npos)
			return path;
		return path.substr(slash + 1);
	}

	static std::string envHeaderName(const std::string& header) {
		std::string out = "HTTP_";
		for (size_t i = 0; i < header.size(); ++i) {
			char c = header[i];
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
			else if (c == '-')
				c = '_';
			out += c;
		}
		return out;
	}
}

// Orthodox Canonical Form
CgiHandler::CgiHandler(HttpRequest& request, HttpResponse& response) : _request(request), _response(response), _exitStatus(0), _pid(-1), _inputFd(-1), _outputFd(-1), _inputOpen(false), _outputOpen(false), _processDone(false), _inputWritten(0), _serverPort(80), _startTime(0) {
}

CgiHandler::CgiHandler(const CgiHandler& other) : _request(other._request), _response(other._response) {
	_scriptPath = other._scriptPath;
	_cgiExecutable = other._cgiExecutable;
	_env = other._env;
	_output = other._output;
	_exitStatus = other._exitStatus;
	_pid = other._pid;
	_inputFd = other._inputFd;
	_outputFd = other._outputFd;
	_inputOpen = other._inputOpen;
	_outputOpen = other._outputOpen;
	_processDone = other._processDone;
	_inputWritten = other._inputWritten;
	_input = other._input;
	_startTime = other._startTime;
	_serverPort = other._serverPort;
	_scriptName = other._scriptName;
	_pathInfo = other._pathInfo;
	_pathTranslated = other._pathTranslated;
	_remoteAddr = other._remoteAddr;
}

CgiHandler& CgiHandler::operator=(const CgiHandler& other) {
	if (this != &other) {
		_scriptPath = other._scriptPath;
		_cgiExecutable = other._cgiExecutable;
		_env = other._env;
		_output = other._output;
		_exitStatus = other._exitStatus;
		_pid = other._pid;
		_inputFd = other._inputFd;
		_outputFd = other._outputFd;
		_inputOpen = other._inputOpen;
		_outputOpen = other._outputOpen;
		_processDone = other._processDone;
		_inputWritten = other._inputWritten;
		_input = other._input;
		_startTime = other._startTime;
		_serverPort = other._serverPort;
		_scriptName = other._scriptName;
		_pathInfo = other._pathInfo;
		_pathTranslated = other._pathTranslated;
		_remoteAddr = other._remoteAddr;
	}
	return *this;
}

CgiHandler::~CgiHandler() {
	closeInput();
	closeOutput();
}

// CGI execution
bool CgiHandler::start(const std::string& scriptPath, const Route& route) {
	_scriptPath = scriptPath;
	_cgiExecutable = getCgiExecutablePath(extensionWithDot(scriptPath), route);
	if (!_cgiExecutable.empty() && _cgiExecutable[0] != '/') {
		char resolved[PATH_MAX];
		if (::realpath(_cgiExecutable.c_str(), resolved) != NULL)
			_cgiExecutable = resolved;
	}
	_output.clear();
	_exitStatus = 0;
	_pid = -1;
	_inputFd = -1;
	_outputFd = -1;
	_inputOpen = false;
	_outputOpen = false;
	_processDone = false;
	_inputWritten = 0;
	_input = _request.getBody();
	_startTime = std::time(NULL);

	if (_cgiExecutable.empty()) {
		_response.setStatusCode(404);
		_response.setContentType("text/html");
		_response.setBody("<html><body><h1>404 Not Found</h1></body></html>");
		return false;
	}
	if (::access(_cgiExecutable.c_str(), X_OK) != 0) {
		_response.setStatusCode(502);
		_response.setContentType("text/html");
		_response.setBody("<html><body><h1>502 Bad Gateway</h1></body></html>");
		return false;
	}

	setupEnvironment(route);

	int inputPipe[2];
	int outputPipe[2];
	if (::pipe(inputPipe) != 0)
		return false;
	if (::pipe(outputPipe) != 0) {
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		return false;
	}

	pid_t pid = ::fork();
	if (pid < 0) {
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		::close(outputPipe[0]);
		::close(outputPipe[1]);
		return false;
	}

	if (pid == 0) {
		::dup2(inputPipe[0], STDIN_FILENO);
		::dup2(outputPipe[1], STDOUT_FILENO);
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		::close(outputPipe[0]);
		::close(outputPipe[1]);

		std::string scriptDir = directoryOf(_scriptPath);
		std::string scriptName = fileNameOf(_scriptPath);
		::chdir(scriptDir.c_str());

		char** env = getEnvArray();
		char* argv[3];
		argv[0] = const_cast<char*>(_cgiExecutable.c_str());
		argv[1] = const_cast<char*>(scriptName.c_str());
		argv[2] = NULL;
		::execve(_cgiExecutable.c_str(), argv, env);
		freeEnvArray(env);
		_exit(1);
	}

	::close(inputPipe[0]);
	::close(outputPipe[1]);
	::fcntl(inputPipe[1], F_SETFL, O_NONBLOCK);
	::fcntl(outputPipe[0], F_SETFL, O_NONBLOCK);
	_pid = pid;
	_inputFd = inputPipe[1];
	_outputFd = outputPipe[0];
	_inputOpen = true;
	_outputOpen = true;
	if (_input.empty())
		closeInput();

	std::ostringstream oss;
	oss << "CGI started [script=" << _scriptPath << ", interpreter=" << _cgiExecutable << ", pid=" << _pid << "]";
	Logger::getInstance()->info(oss.str());
	return true;
}

ssize_t CgiHandler::writeInput() {
	if (!_inputOpen || _inputFd < 0)
		return 0;
	if (_inputWritten >= _input.size()) {
		closeInput();
		return 0;
	}
	size_t remaining = _input.size() - _inputWritten;
	size_t chunkSize = remaining > 8192 ? 8192 : remaining;
	ssize_t bytes = ::write(_inputFd, _input.c_str() + _inputWritten, chunkSize);
	if (bytes > 0) {
		_inputWritten += static_cast<size_t>(bytes);
		if (_inputWritten >= _input.size())
			closeInput();
		return bytes;
	}
	if (bytes < 0) {
		if (!_processDone && _pid > 0 && ::waitpid(_pid, &_exitStatus, WNOHANG) == _pid)
			_processDone = true;
		if (_processDone)
			closeInput();
	}
	return bytes;
}

ssize_t CgiHandler::readOutput() {
	if (!_outputOpen || _outputFd < 0)
		return 0;
	char buffer[8192];
	ssize_t bytes = ::read(_outputFd, buffer, sizeof(buffer));
	if (bytes > 0)
		_output.append(buffer, static_cast<size_t>(bytes));
	else if (bytes == 0)
		closeOutput();
	return bytes;
}

void CgiHandler::closeInput() {
	if (_inputFd >= 0)
		::close(_inputFd);
	_inputFd = -1;
	_inputOpen = false;
}

void CgiHandler::closeOutput() {
	if (_outputFd >= 0)
		::close(_outputFd);
	_outputFd = -1;
	_outputOpen = false;
}

bool CgiHandler::isComplete() {
	if (_pid > 0 && !_processDone) {
		pid_t waited = ::waitpid(_pid, &_exitStatus, WNOHANG);
		if (waited == _pid)
			_processDone = true;
	}
	return !_inputOpen && !_outputOpen && _processDone;
}

bool CgiHandler::isTimeout(time_t now, time_t timeout) const {
	return _pid > 0 && (now - _startTime) > timeout;
}

void CgiHandler::killProcess() {
	if (_pid > 0 && !_processDone) {
		::kill(_pid, SIGKILL);
		::waitpid(_pid, &_exitStatus, 0);
		_processDone = true;
		std::ostringstream oss;
		oss << "CGI killed [script=" << _scriptPath << ", pid=" << _pid << "]";
		Logger::getInstance()->warning(oss.str());
	}
	closeInput();
	closeOutput();
}

void CgiHandler::finish() {
	if (_pid > 0 && !_processDone) {
		pid_t waited = ::waitpid(_pid, &_exitStatus, WNOHANG);
		if (waited == _pid)
			_processDone = true;
	}
	if (WIFEXITED(_exitStatus) && WEXITSTATUS(_exitStatus) != 0 && _output.empty()) {
		std::ostringstream oss;
		oss << "CGI failed [script=" << _scriptPath << ", pid=" << _pid
			<< ", exit=" << WEXITSTATUS(_exitStatus) << "]";
		Logger::getInstance()->error(oss.str());
		setGatewayError(502);
		return;
	}
	parseOutput();
}

void CgiHandler::setGatewayError(int statusCode) {
	_response.setStatusCode(statusCode);
	_response.setContentType("text/html");
	if (statusCode == 504)
		_response.setBody("<html><body><h1>504 Gateway Timeout</h1></body></html>");
	else
		_response.setBody("<html><body><h1>502 Bad Gateway</h1></body></html>");
}

void CgiHandler::setEnvVariable(const std::string& key, const std::string& value) {
	_env[key] = value;
}

// Private helper methods
void CgiHandler::setupEnvironment(const Route& route) {
	(void)route;
	_env.clear();
	_env["GATEWAY_INTERFACE"] = "CGI/1.1";
	_env["SERVER_SOFTWARE"] = "webserv/0.1";
	_env["SERVER_PROTOCOL"] = _request.getHttpVersion();
	_env["SERVER_PORT"] = toString(_serverPort);
	_env["REQUEST_METHOD"] = _request.getMethod();
	_env["REQUEST_URI"] = _request.getUri();
	_env["SCRIPT_NAME"] = _scriptName.empty() ? _request.getPath() : _scriptName;
	_env["SCRIPT_FILENAME"] = _scriptPath;
	_env["QUERY_STRING"] = _request.getQueryString();
	_env["PATH_INFO"] = _pathInfo;
	_env["PATH_TRANSLATED"] = _pathInfo.empty() ? "" : _pathTranslated;
	if (!_remoteAddr.empty())
		_env["REMOTE_ADDR"] = _remoteAddr;
	_env["REDIRECT_STATUS"] = "200";
	_env["CONTENT_LENGTH"] = toString(_request.getBody().size());
	if (_request.hasHeader("content-type"))
		_env["CONTENT_TYPE"] = _request.getHeader("content-type");
	if (_request.hasHeader("host")) {
		std::string host = _request.getHeader("host");
		size_t colon = host.find(':');
		if (colon == std::string::npos) {
			_env["SERVER_NAME"] = host;
		} else {
			_env["SERVER_NAME"] = host.substr(0, colon);
		}
	} else {
		_env["SERVER_NAME"] = "localhost";
	}

	const std::map<std::string, std::string>& headers = _request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
		if (it->first == "content-type" || it->first == "content-length")
			continue;
		_env[envHeaderName(it->first)] = it->second;
	}
}

void CgiHandler::parseOutput() {
	size_t headerEnd = _output.find("\r\n\r\n");
	size_t sepLength = 4;
	if (headerEnd == std::string::npos) {
		headerEnd = _output.find("\n\n");
		sepLength = 2;
	}

	if (headerEnd == std::string::npos) {
		_response.setStatusCode(200);
		_response.setContentType("text/plain");
		_response.setBody(_output);
		return;
	}

	std::string headerBlock = _output.substr(0, headerEnd);
	std::string body = _output.substr(headerEnd + sepLength);
	std::istringstream iss(headerBlock);
	std::string line;
	bool hasContentType = false;
	_response.setStatusCode(200);

	while (std::getline(iss, line)) {
		line = trim(line);
		if (line.empty())
			continue;
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = trim(line.substr(0, colon));
		std::string value = trim(line.substr(colon + 1));
		if (key == "Status") {
			_response.setStatusCode(std::atoi(value.c_str()));
		} else if (key == "Content-Type") {
			_response.setContentType(value);
			hasContentType = true;
		} else if (key != "Content-Length") {
			_response.addHeader(key, value);
		}
	}

	if (!hasContentType)
		_response.setContentType("text/plain");
	_response.setBody(body);
}

char** CgiHandler::getEnvArray() const {
	char** env = new char*[_env.size() + 1];
	size_t i = 0;
	for (std::map<std::string, std::string>::const_iterator it = _env.begin(); it != _env.end(); ++it) {
		std::string entry = it->first + "=" + it->second;
		env[i] = new char[entry.size() + 1];
		std::strcpy(env[i], entry.c_str());
		++i;
	}
	env[i] = NULL;
	return env;
}

void CgiHandler::freeEnvArray(char** env) const {
	if (env == NULL)
		return;
	for (size_t i = 0; env[i] != NULL; ++i)
		delete[] env[i];
	delete[] env;
}

// Getters
int CgiHandler::getInputFd() const {
	return _inputFd;
}

int CgiHandler::getOutputFd() const {
	return _outputFd;
}

bool CgiHandler::wantsInputWrite() const {
	return _inputOpen && _inputFd >= 0;
}

bool CgiHandler::wantsOutputRead() const {
	return _outputOpen && _outputFd >= 0;
}

// Setters
void CgiHandler::setServerPort(int port) {
	_serverPort = port;
}

void CgiHandler::setScriptName(const std::string& scriptName) {
	_scriptName = scriptName;
}

void CgiHandler::setPathInfo(const std::string& pathInfo, const std::string& pathTranslated) {
	_pathInfo = pathInfo;
	_pathTranslated = pathTranslated;
}

void CgiHandler::setRemoteAddr(const std::string& remoteAddr) {
	_remoteAddr = remoteAddr;
}

// Static utility methods
bool CgiHandler::isCgiRequest(const std::string& path, const Route& route) {
	std::string ext = extensionWithDot(path);
	return !ext.empty() && route.hasCgiExtension(ext);
}

std::string CgiHandler::getCgiExecutablePath(const std::string& extension, const Route& route) {
	return route.getCgiHandler(extension);
}
