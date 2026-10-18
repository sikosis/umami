#ifndef UMAMI_PATHS_H
#define UMAMI_PATHS_H

#include <filesystem>
#include <string>

namespace umami {

struct AppPaths {
	std::filesystem::path settingsDirectory;
	std::filesystem::path cacheDirectory;
	std::filesystem::path catalogFile;
	std::filesystem::path stateFile;

	static bool FromEnvironment(AppPaths& paths, std::string& error);
	static AppPaths FromHome(const std::filesystem::path& homeDirectory);
	bool EnsureDirectories(std::string& error) const;
};

}

#endif
