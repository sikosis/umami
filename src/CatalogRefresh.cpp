#include "CatalogRefresh.h"

#include "Catalog.h"
#include "Logger.h"
#include "ProcessRunner.h"

#include <filesystem>
#include <vector>

namespace umami {

bool InstallDownloadedCatalog(const std::filesystem::path& downloadedFile,
	const std::filesystem::path& catalogFile, std::string& error) {
	error.clear();
	Catalog catalog;
	if (!catalog.Load(downloadedFile, error)) {
		error = "downloaded catalog is invalid: " + error;
		return false;
	}

	std::error_code status;
	std::filesystem::rename(downloadedFile, catalogFile, status);
	if (status) {
		error = "could not replace catalog '" + catalogFile.string() + "': " + status.message();
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool RefreshCatalog(const std::filesystem::path& catalogFile, const ProcessRunner& runner,
	const Logger& logger, std::string& error) {
	error.clear();
	std::filesystem::path downloadedFile = catalogFile;
	downloadedFile += ".download";

	std::error_code status;
	std::filesystem::remove(downloadedFile, status);
	if (status) {
		error = "could not remove stale download '" + downloadedFile.string() + "': " + status.message();
		return false;
	}

	std::vector<std::string> arguments = {
		"curl",
		"--fail",
		"--location",
		"--silent",
		"--show-error",
		"--connect-timeout",
		"10",
		"--max-time",
		"60",
		"--output",
		downloadedFile.string(),
		kDefaultCatalogUrl
	};
	logger.Command(arguments);
	ProcessResult result = runner.Run(arguments);
	if (!result.started) {
		error = result.error;
		return false;
	}
	if (result.exitCode != 0) {
		error = "catalog download failed";
		if (!result.output.empty()) {
			while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r'))
				result.output.pop_back();
			error += ": " + result.output;
		}
		std::filesystem::remove(downloadedFile, status);
		return false;
	}

	if (!InstallDownloadedCatalog(downloadedFile, catalogFile, error)) {
		std::filesystem::remove(downloadedFile, status);
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
