#ifndef UMAMI_CATALOG_REFRESH_H
#define UMAMI_CATALOG_REFRESH_H

#include <filesystem>
#include <string>

namespace umami {

class Logger;
class ProcessRunner;

constexpr const char* kDefaultCatalogUrl
	= "https://raw.githubusercontent.com/sikosis/umami-cookbook/main/catalog.json";

bool InstallDownloadedCatalog(const std::filesystem::path& downloadedFile,
	const std::filesystem::path& catalogFile, std::string& error);
bool RefreshCatalog(const std::filesystem::path& catalogFile, const ProcessRunner& runner,
	const Logger& logger, std::string& error);

}

#endif
