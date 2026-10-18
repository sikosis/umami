#include "Catalog.h"
#include "CatalogRefresh.h"
#include "InstalledState.h"
#include "Paths.h"
#include "ProcessRunner.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int sFailures = 0;
std::string sProgramPath;

void Check(bool condition, const std::string& message) {
	if (condition)
		return;
	std::cerr << "FAIL: " << message << '\n';
	++sFailures;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool WriteText(const std::filesystem::path& path, const std::string& contents) {
	std::ofstream output(path);
	output << contents;
	return output.good();
}
//---------------------------------------------------------------------------------------------------------------------------------//

void TestProcessOutput() {
	umami::ProcessRunner runner;
	umami::ProcessResult result = runner.Run({"/bin/echo", "umami"});
	Check(result.started, "echo should start");
	Check(result.exitCode == 0, "echo should exit successfully");
	Check(result.output == "umami\n", "echo output should be captured");
	Check(result.error.empty(), "echo should not produce a runner error");
}
//---------------------------------------------------------------------------------------------------------------------------------//

void TestProcessFailure() {
	umami::ProcessRunner runner;
	umami::ProcessResult result = runner.Run({"/definitely/not/a/program"});
	Check(!result.started, "missing command should not start");
	Check(result.exitCode == -1, "missing command should not have an exit code");
	Check(!result.error.empty(), "missing command should report an error");

	result = runner.Run({});
	Check(!result.started, "empty command should not start");
	Check(result.error == "cannot run an empty command", "empty command should report a useful error");

	result = runner.Run({sProgramPath, "--return-failure"});
	Check(result.started, "test helper should start");
	Check(result.exitCode == 7, "non-zero process exit code should be preserved");
}
//---------------------------------------------------------------------------------------------------------------------------------//

void TestPaths() {
	std::filesystem::path home = std::filesystem::temp_directory_path()
		/ ("umami-tests-" + std::to_string(static_cast<long long>(getpid())));
	umami::AppPaths paths = umami::AppPaths::FromHome(home);
	Check(paths.catalogFile == home / "config/settings/umami/catalog.json", "catalog path should be under settings");
	Check(paths.stateFile == home / "config/settings/umami/installed.json", "state path should be under settings");

	std::string error;
	Check(paths.EnsureDirectories(error), "application directories should be created: " + error);
	Check(std::filesystem::is_directory(paths.settingsDirectory), "settings directory should exist");
	Check(std::filesystem::is_directory(paths.cacheDirectory), "cache directory should exist");
	std::error_code status;
	std::filesystem::remove_all(home, status);
	Check(!status, "test directory should be removable");
}
//---------------------------------------------------------------------------------------------------------------------------------//

void TestCatalogAndState() {
	std::filesystem::path directory = std::filesystem::temp_directory_path()
		/ ("umami-catalog-tests-" + std::to_string(static_cast<long long>(getpid())));
	std::error_code status;
	std::filesystem::create_directories(directory, status);
	Check(!status, "catalog test directory should be created");

	std::filesystem::path catalogPath = directory / "catalog.json";
	const std::string catalogJson = R"({
  "version": 1,
  "apps": [{
    "id": "exampleapp",
    "name": "Example App",
    "description": "Example description",
    "provider": "github",
    "repo": "someone/exampleapp",
    "asset_pattern": "^example-.*-{arch}\\.hpkg$",
    "package_name": "exampleapp",
    "channel": "stable",
    "checksum_asset": "{asset}.sha256"
  }]
})";
	Check(WriteText(catalogPath, catalogJson), "valid catalog fixture should be written");
	umami::Catalog catalog;
	std::string error;
	Check(catalog.Load(catalogPath, error), "valid catalog should load: " + error);
	const umami::CatalogEntry* catalogEntry = catalog.Find("exampleapp");
	Check(catalogEntry != nullptr, "catalog entry should be found by id");
	if (catalogEntry != nullptr)
		Check(catalogEntry->packageName == "exampleapp", "package name should be parsed");

	std::filesystem::path statePath = directory / "installed.json";
	const std::string stateJson = R"({
  "version": 1,
  "installed": {
    "exampleapp": {
      "package_name": "exampleapp",
      "package_version": "1.2.0-1",
      "release_tag": "v1.2.0",
      "asset": "exampleapp-1.2.0-1-x86_64.hpkg",
      "sha256": "abc123",
      "scope": "user",
      "installed_at": "2026-10-14T21:40:00+10:00"
    }
  }
})";
	Check(WriteText(statePath, stateJson), "valid state fixture should be written");
	umami::InstalledState state;
	Check(state.Load(statePath, error), "valid state should load: " + error);
	const umami::InstalledEntry* installedEntry = state.Find("exampleapp");
	Check(installedEntry != nullptr, "installed entry should be found by id");
	if (installedEntry != nullptr)
		Check(installedEntry->packageVersion == "1.2.0-1", "installed version should be parsed");

	Check(WriteText(catalogPath, "{\"version\":1,\"apps\":[{\"id\":\"broken\"}]}"),
		"invalid catalog fixture should be written");
	Check(!catalog.Load(catalogPath, error), "catalog with missing fields should fail");
	Check(error.find("catalog app 1") != std::string::npos, "catalog error should identify the failing entry");

	std::filesystem::remove_all(directory, status);
	Check(!status, "catalog test directory should be removable");
}
//---------------------------------------------------------------------------------------------------------------------------------//

void TestDownloadedCatalogInstallation() {
	std::filesystem::path directory = std::filesystem::temp_directory_path()
		/ ("umami-refresh-tests-" + std::to_string(static_cast<long long>(getpid())));
	std::error_code status;
	std::filesystem::create_directories(directory, status);
	Check(!status, "refresh test directory should be created");

	std::filesystem::path catalogPath = directory / "catalog.json";
	std::filesystem::path downloadedPath = directory / "catalog.json.download";
	const std::string originalCatalog = R"({"version":1,"apps":[]})";
	const std::string replacementCatalog = R"({
  "version": 1,
  "apps": [{
    "id": "replacement",
    "name": "Replacement",
    "description": "Replacement catalog entry",
    "provider": "github",
    "repo": "someone/replacement",
    "asset_pattern": "^replacement-.*-{arch}\\.hpkg$",
    "package_name": "replacement",
    "channel": "stable"
  }]
})";
	Check(WriteText(catalogPath, originalCatalog), "original catalog fixture should be written");
	Check(WriteText(downloadedPath, replacementCatalog), "downloaded catalog fixture should be written");
	std::string error;
	Check(umami::InstallDownloadedCatalog(downloadedPath, catalogPath, error),
		"valid downloaded catalog should be installed: " + error);
	Check(!std::filesystem::exists(downloadedPath), "installed download should be renamed away");
	umami::Catalog catalog;
	Check(catalog.Load(catalogPath, error), "installed catalog should load: " + error);
	Check(catalog.Find("replacement") != nullptr, "installed catalog should contain replacement entry");

	Check(WriteText(downloadedPath, "{\"version\":1,\"apps\":[{\"id\":\"broken\"}]}"),
		"invalid downloaded catalog fixture should be written");
	Check(!umami::InstallDownloadedCatalog(downloadedPath, catalogPath, error),
		"invalid downloaded catalog should be rejected");
	Check(catalog.Load(catalogPath, error), "previous catalog should remain after a rejected download: " + error);
	Check(catalog.Find("replacement") != nullptr, "rejected download should not replace the catalog");

	std::filesystem::remove_all(directory, status);
	Check(!status, "refresh test directory should be removable");
}
//---------------------------------------------------------------------------------------------------------------------------------//

}

int main(int argumentCount, char** arguments) {
	if (argumentCount == 2 && std::string(arguments[1]) == "--return-failure")
		return 7;
	sProgramPath = arguments[0];

	TestProcessOutput();
	TestProcessFailure();
	TestPaths();
	TestCatalogAndState();
	TestDownloadedCatalogInstallation();
	if (sFailures != 0) {
		std::cerr << sFailures << " test(s) failed\n";
		return 1;
	}
	std::cout << "All tests passed\n";
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//
