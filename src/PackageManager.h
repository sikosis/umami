#ifndef UMAMI_PACKAGE_MANAGER_H
#define UMAMI_PACKAGE_MANAGER_H

#include "Catalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace umami {

class Logger;
class ProcessRunner;

struct ReleaseAsset {
	std::string name;
	std::string downloadUrl;
};

struct ReleaseInfo {
	std::string tag;
	ReleaseAsset asset;
};

struct PackageDetails {
	std::string name;
	std::string version;
	std::string architecture;
};

bool DetectArchitecture(const ProcessRunner& runner, const Logger& logger,
	std::string& architecture, std::string& error);
bool ParseReleaseFile(const std::filesystem::path& path, const CatalogEntry& entry,
	const std::string& architecture, bool findPrerelease, ReleaseInfo& release, std::string& error);
bool ResolveRelease(const CatalogEntry& entry, const std::string& requestedTag,
	const std::string& architecture, const std::filesystem::path& cacheDirectory,
	const ProcessRunner& runner, const Logger& logger, ReleaseInfo& release, std::string& error);
bool DownloadReleaseAsset(const ReleaseAsset& asset, const std::filesystem::path& cacheDirectory,
	const ProcessRunner& runner, const Logger& logger, std::filesystem::path& packageFile, std::string& error);
bool InspectPackage(const std::filesystem::path& packageFile, const ProcessRunner& runner,
	const Logger& logger, PackageDetails& details, std::string& error);
bool ParsePackageInfo(const std::string& output, PackageDetails& details, std::string& error);
bool ComputeSha256(const std::filesystem::path& packageFile, const ProcessRunner& runner,
	const Logger& logger, std::string& checksum, std::string& error);
bool IsPackageInstalled(const std::string& packageName, const ProcessRunner& runner,
	const Logger& logger, bool& installed, std::string& error);
bool RunPackageCommand(const std::vector<std::string>& arguments, const ProcessRunner& runner,
	const Logger& logger, std::string& output, std::string& error);

}

#endif
