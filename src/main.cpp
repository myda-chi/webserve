#include "../include/Server.hpp"
#include "../include/ConfigParser.hpp"
#include "../include/Logger.hpp"
#include "../include/FileRegistry.hpp"
#include <iostream>
#include <cstdlib>
#include <csignal>

Server* g_server = NULL;

void signalHandler(int signal) {
	if (signal == SIGINT || signal == SIGTERM) {
		Logger::getInstance()->info("Received shutdown signal");
		if (g_server) {
			g_server->stop();
		}
		Logger::destroy();
		exit(0);
	}
}

void setupSignalHandlers() {
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGPIPE, SIG_IGN);
}

void printUsage(const char* programName) {
	std::cout << "Usage: " << programName << " [configuration_file]" << std::endl;
	std::cout << "  configuration_file: Path to server configuration file (optional)" << std::endl;
	std::cout << "                     Default: config/default.conf" << std::endl;
}

int main(int argc, char* argv[]) {
	std::string configFile;

	// Parse command line arguments
	if (argc > 2) {
		std::cerr << "Error: Too many arguments" << std::endl;
		printUsage(argv[0]);
		return 1;
	}

	if (argc == 2) {
		if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
			printUsage(argv[0]);
			return 0;
		}
		configFile = argv[1];
	} else {
		configFile = ConfigParser::getDefaultConfigPath();
	}

	// Initialize logger
	Logger* logger = Logger::getInstance();
	logger->setMinLevel(INFO);
	logger->enableConsole(true);
	logger->setLogFile("webserv.log");
	logger->enableFile(true);

	logger->info("=== Webserv HTTP Server ===");
	logger->info("Configuration file: " + configFile);

	try {
		// Parse configuration
		ConfigParser parser(configFile);
		if (!parser.parse()) {
			logger->error("Failed to parse configuration file");
			return 1;
		}

		// Create and initialize server
		Server server;
		g_server = &server;

		std::vector<ServerConfig> configs = parser.getServerConfigs();
		for (size_t i = 0; i < configs.size(); ++i) {
			server.addServerConfig(configs[i]);
			const std::vector<Route>& routes = configs[i].getRoutes();
			for (size_t j = 0; j < routes.size(); ++j)
				if (!routes[j].getUploadPath().empty())
					FileRegistry::getInstance().loadFromDirectory(routes[j].getUploadPath());
		}

		// Setup signal handlers
		setupSignalHandlers();

		// Initialize and run server
		server.init();
		logger->info("Server initialized successfully");
		logger->info("Starting server...");
		server.run();

	} catch (const std::exception& e) {
		logger->fatal(std::string("Fatal error: ") + e.what());
		Logger::destroy();
		return 1;
	}

	logger->info("Server stopped gracefully");
	Logger::destroy();
	return 0;
}
