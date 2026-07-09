#ifndef ROUTE_HPP
# define ROUTE_HPP

# include <string>
# include <vector>
# include <map>

class Route {
private:
	std::string						_path;
	std::vector<std::string>		_allowedMethods;
	std::string						_root;
	bool							_autoindex;
	std::vector<std::string>		_index;
	std::string						_redirect;
	int								_redirectCode;
	std::string						_uploadPath;
	std::map<std::string, std::string>	_cgiExtensions;
	size_t							_clientMaxBodySize;
	bool							_hasClientMaxBodySize;

public:
	// Orthodox Canonical Form
	Route();
	Route(const std::string& path);
	Route(const Route& other);
	Route& operator=(const Route& other);
	~Route();

	// Setters
	void		addAllowedMethod(const std::string& method);
	void		setRoot(const std::string& root);
	void		setAutoindex(bool autoindex);
	void		addIndexFile(const std::string& file);
	void		setRedirect(const std::string& redirect);
	void		setRedirectCode(int code);
	void		setUploadPath(const std::string& path);
	void		addCgiExtension(const std::string& ext, const std::string& handler);
	void		setClientMaxBodySize(size_t size);

	// Getters
	const std::string&						getPath() const;
	const std::vector<std::string>&			getAllowedMethods() const;
	const std::string&						getRoot() const;
	bool									getAutoindex() const;
	const std::vector<std::string>&			getIndex() const;
	const std::string&						getRedirect() const;
	int										getRedirectCode() const;
	const std::string&						getUploadPath() const;
	bool									hasClientMaxBodySize() const;
	size_t									getClientMaxBodySize() const;

	// Utility methods
	// isMethodAllowed: an empty allowed_methods list means every method is
	// allowed (nginx-like default).
	bool		isMethodAllowed(const std::string& method) const;
	bool		hasCgiExtension(const std::string& ext) const;
	std::string	getCgiHandler(const std::string& ext) const;
	// Prefix match on whole path segments: "/kapouet" matches /kapouet and
	// /kapouet/x but not /kapouetXYZ.
	bool		matches(const std::string& requestPath) const;
};

#endif
