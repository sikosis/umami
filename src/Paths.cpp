#include "Paths.h"

#include <cstdlib>
#include <system_error>

namespace umami {

bool AppPaths::FromEnvironment(AppPaths& paths, std::string& error) {
	const char* home = std::getenv("HOME");
	if (home == nullptr || home[0] == '\0') {
		error = "HOME is not set; cannot locate umami settings";
		return false;
	}

	paths = FromHome(home);
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

AppPaths AppPaths::FromHome(const std::filesystem::path& homeDirectory) {
	AppPaths paths;
	paths.settingsDirectory = homeDirectory / "config" / "settings" / "umami";
	paths.cacheDirectory = homeDirectory / "config" / "cache" / "umami";
	paths.recipeDirectory = paths.settingsDirectory / "recipes";
	paths.catalogFile = paths.settingsDirectory / "catalog.json";
	paths.stateFile = paths.settingsDirectory / "installed.json";
	return paths;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool AppPaths::EnsureDirectories(std::string& error) const {
	std::error_code status;
	std::filesystem::create_directories(settingsDirectory, status);
	if (status) {
		error = "could not create settings directory '" + settingsDirectory.string() + "': " + status.message();
		return false;
	}

	std::filesystem::create_directories(cacheDirectory, status);
	if (status) {
		error = "could not create cache directory '" + cacheDirectory.string() + "': " + status.message();
		return false;
	}

	std::filesystem::create_directories(recipeDirectory, status);
	if (status) {
		error = "could not create recipe directory '" + recipeDirectory.string() + "': " + status.message();
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
