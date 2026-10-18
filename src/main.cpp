#include "Catalog.h"
#include "CatalogRefresh.h"
#include "InstalledState.h"
#include "Logger.h"
#include "PackageManager.h"
#include "Paths.h"
#include "ProcessRunner.h"
#include "RecipeManager.h"
#include "Version.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

struct CommandLine {
	bool verbose = false;
	bool installedOnly = false;
	bool assumeYes = false;
	std::filesystem::path catalog;
	std::string command;
	std::vector<std::string> operands;
};

void PrintUsage(std::ostream& output) {
	output
		<< "Usage: umami [common options] <command> [arguments]\n"
		<< "\n"
		<< "Commands:\n"
		<< "  list [--installed]       List catalog applications\n"
		<< "  info <id>                Show application details\n"
		<< "  install <id>[@tag]       Install an application\n"
		<< "  remove <id>              Remove an installed application\n"
		<< "  update [<id>]            Update one or all tracked applications\n"
		<< "  refresh                  Download the latest cookbook catalog\n"
		<< "  recipe add <owner/repo>  Generate and review a cookbook recipe\n"
		<< "  recipe publish <id>      Commit a reviewed recipe to the cookbook\n"
		<< "  doctor                   Check tools, cache and state (planned)\n"
		<< "\n"
		<< "Common options:\n"
		<< "  -y                       Answer yes to confirmation prompts\n"
		<< "  --catalog <file>         Use an alternative catalog\n"
		<< "  -v                       Print diagnostic information\n"
		<< "  --help                   Show this help\n"
		<< "  --version                Show the version\n";
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ParseCommandLine(int argumentCount, char** arguments, CommandLine& commandLine, int& exitCode) {
	for (int index = 1; index < argumentCount; ++index) {
		std::string argument = arguments[index];
		if (argument == "--help" || argument == "-h") {
			PrintUsage(std::cout);
			exitCode = 0;
			return false;
		}
		if (argument == "--version") {
			std::cout << "umami v" << umami::kVersion << '\n';
			exitCode = 0;
			return false;
		}
		if (argument == "-v") {
			commandLine.verbose = true;
			continue;
		}
		if (argument == "-y") {
			commandLine.assumeYes = true;
			continue;
		}
		if (argument == "--catalog") {
			if (++index >= argumentCount) {
				std::cerr << "umami: --catalog requires a file path\n";
				exitCode = 1;
				return false;
			}
			commandLine.catalog = arguments[index];
			continue;
		}
		if (argument == "--installed") {
			commandLine.installedOnly = true;
			continue;
		}
		if (!argument.empty() && argument.front() == '-') {
			std::cerr << "umami: unknown option '" << argument << "'\n";
			exitCode = 1;
			return false;
		}
		if (commandLine.command.empty())
			commandLine.command = argument;
		else
			commandLine.operands.push_back(argument);
	}
	if (commandLine.command.empty()) {
		PrintUsage(std::cerr);
		exitCode = 1;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ValidateCommand(const CommandLine& commandLine, const umami::Logger& logger, int& exitCode) {
	if (commandLine.command != "list" && commandLine.command != "info" && commandLine.command != "install"
		&& commandLine.command != "remove" && commandLine.command != "update" && commandLine.command != "refresh"
		&& commandLine.command != "recipe") {
		logger.Error("command '" + commandLine.command + "' is not implemented in v" + umami::kVersion);
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "list" && !commandLine.operands.empty()) {
		logger.Error("usage: umami list [--installed]");
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "info" && commandLine.operands.size() != 1) {
		logger.Error("usage: umami info <id>");
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "refresh" && !commandLine.operands.empty()) {
		logger.Error("usage: umami refresh");
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "install" && commandLine.operands.size() != 1) {
		logger.Error("usage: umami install <id>[@tag]");
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "remove" && commandLine.operands.size() != 1) {
		logger.Error("usage: umami remove <id>");
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "update" && commandLine.operands.size() > 1) {
		logger.Error("usage: umami update [<id>]");
		exitCode = 1;
		return false;
	}
	if (commandLine.command == "recipe" && (commandLine.operands.size() != 2
		|| (commandLine.operands.front() != "add" && commandLine.operands.front() != "publish"))) {
		logger.Error("usage: umami recipe add <owner/repository> | recipe publish <id>");
		exitCode = 1;
		return false;
	}
	if (commandLine.command != "list" && commandLine.installedOnly) {
		logger.Error("--installed can only be used with list");
		exitCode = 1;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

int ListApplications(const umami::Catalog& catalog, const umami::InstalledState& state, bool installedOnly,
	const std::string& catalogSource) {
	std::cout << "Catalog: " << catalogSource << "\n\n";
	std::cout << std::left << std::setw(20) << "ID" << std::setw(32) << "NAME" << "VERSION\n";
	for (const umami::CatalogEntry& entry : catalog.Entries()) {
		const umami::InstalledEntry* installed = state.Find(entry.id);
		if (installedOnly && installed == nullptr)
			continue;
		std::cout << std::left << std::setw(20) << entry.id << std::setw(32) << entry.name
			<< (installed == nullptr ? "-" : installed->packageVersion) << '\n';
	}
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool Confirm(const std::string& prompt, bool assumeYes) {
	if (assumeYes)
		return true;
	std::cout << prompt << " [y/N] " << std::flush;
	std::string response;
	if (!std::getline(std::cin, response))
		return false;
	return response == "y" || response == "Y" || response == "yes" || response == "YES";
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ReadGitHubToken(std::string& token, std::string& error) {
	const char* environmentToken = std::getenv("UMAMI_GITHUB_TOKEN");
	if (environmentToken != nullptr && environmentToken[0] != '\0') {
		token = environmentToken;
		return true;
	}
	if (!isatty(STDIN_FILENO)) {
		error = "set UMAMI_GITHUB_TOKEN when publishing without an interactive terminal";
		return false;
	}
	termios original = {};
	if (tcgetattr(STDIN_FILENO, &original) != 0) {
		error = "could not disable terminal echo for the GitHub token";
		return false;
	}
	termios hidden = original;
	hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) {
		error = "could not disable terminal echo for the GitHub token";
		return false;
	}
	std::cout << "GitHub token (contents:write for sikosis/umami-cookbook): " << std::flush;
	bool read = static_cast<bool>(std::getline(std::cin, token));
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
	std::cout << '\n';
	if (!read || token.empty()) {
		error = "a GitHub token is required to publish a recipe";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

int PublishRecipeProposal(const std::filesystem::path& recipePath, const umami::AppPaths& paths,
	const umami::ProcessRunner& runner, const umami::Logger& logger) {
	std::string token;
	std::string error;
	if (!ReadGitHubToken(token, error)) {
		logger.Error(error);
		return 1;
	}
	if (!umami::PublishRecipe(recipePath, token, paths.cacheDirectory, runner, logger, error)) {
		std::fill(token.begin(), token.end(), '\0');
		logger.Error(error);
		return 3;
	}
	std::fill(token.begin(), token.end(), '\0');
	std::cout << "Recipe committed to https://github.com/sikosis/umami-cookbook\n";
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::string CurrentTimestamp() {
	std::time_t currentTime = std::time(nullptr);
	std::tm value = {};
	if (gmtime_r(&currentTime, &value) == nullptr)
		return "unknown";
	char buffer[32];
	if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &value) == 0)
		return "unknown";
	return buffer;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool SplitInstallTarget(const std::string& target, std::string& identifier, std::string& tag) {
	std::size_t separator = target.find('@');
	identifier = target.substr(0, separator);
	tag = separator == std::string::npos ? "" : target.substr(separator + 1);
	return !identifier.empty() && (separator == std::string::npos || !tag.empty());
}
//---------------------------------------------------------------------------------------------------------------------------------//

int InstallApplication(const umami::CatalogEntry& entry, const std::string& requestedTag,
	const umami::AppPaths& paths, umami::InstalledState& state, const umami::ProcessRunner& runner,
	const umami::Logger& logger, bool assumeYes, bool updateOnly) {
	std::string error;
	const umami::InstalledEntry* existing = state.Find(entry.id);
	if (updateOnly && existing == nullptr) {
		logger.Error("'" + entry.id + "' is not tracked as installed");
		return 1;
	}
	std::string architecture;
	if (!umami::DetectArchitecture(runner, logger, architecture, error)) {
		logger.Error(error);
		return 4;
	}

	umami::ReleaseInfo release;
	if (!umami::ResolveRelease(entry, requestedTag, architecture, paths.cacheDirectory,
		runner, logger, release, error)) {
		logger.Error(error);
		return 3;
	}
	if (existing != nullptr && existing->releaseTag == release.tag) {
		std::cout << entry.name << " is already at " << release.tag << '\n';
		return 0;
	}
	std::filesystem::path packageFile;
	if (!umami::DownloadReleaseAsset(release.asset, paths.cacheDirectory, runner, logger, packageFile, error)) {
		logger.Error(error);
		return 3;
	}
	umami::PackageDetails details;
	if (!umami::InspectPackage(packageFile, runner, logger, details, error)) {
		logger.Error(error);
		return 4;
	}
	if (details.name != entry.packageName) {
		logger.Error("package name '" + details.name + "' does not match expected name '" + entry.packageName + "'");
		return 4;
	}
	if (details.architecture != architecture && details.architecture != "any") {
		logger.Error("package architecture '" + details.architecture + "' does not match '" + architecture + "'");
		return 4;
	}
	std::string checksum;
	if (!umami::ComputeSha256(packageFile, runner, logger, checksum, error)) {
		logger.Error(error);
		return 4;
	}

	std::cout << (existing == nullptr ? "Install " : "Update ") << entry.name << " to "
		<< details.version << " from " << release.tag << "\n"
		<< "Package: " << packageFile << '\n';
	if (!Confirm("Continue?", assumeYes)) {
		std::cout << "Cancelled\n";
		return 0;
	}
	std::vector<std::string> arguments = {"pkgman", "install", "-H", "-y", packageFile.string()};
	std::string output;
	if (!umami::RunPackageCommand(arguments, runner, logger, output, error)) {
		logger.Error(error);
		return 5;
	}
	if (!output.empty())
		std::cout << output;
	bool packageInstalled = false;
	if (!umami::IsPackageInstalled(entry.packageName, runner, logger, packageInstalled, error)) {
		logger.Error("package installation could not be confirmed: " + error);
		return 5;
	}
	if (!packageInstalled) {
		logger.Error("pkgman completed but package '" + entry.packageName + "' is not active");
		return 5;
	}

	umami::InstalledEntry installed;
	installed.packageName = entry.packageName;
	installed.packageVersion = details.version;
	installed.releaseTag = release.tag;
	installed.asset = release.asset.name;
	installed.sha256 = checksum;
	installed.scope = "user";
	installed.installedAt = CurrentTimestamp();
	state.Set(entry.id, installed);
	if (!state.Save(paths.stateFile, error)) {
		logger.Error("package was installed but state could not be saved: " + error);
		return 2;
	}
	std::cout << entry.name << " " << details.version << " installed\n";
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//

int RemoveApplication(const std::string& identifier, const umami::AppPaths& paths,
	umami::InstalledState& state, const umami::ProcessRunner& runner, const umami::Logger& logger,
	bool assumeYes) {
	const umami::InstalledEntry* installed = state.Find(identifier);
	if (installed == nullptr) {
		logger.Error("'" + identifier + "' is not tracked as installed");
		return 1;
	}
	std::string packageName = installed->packageName;
	if (!Confirm("Remove " + packageName + "?", assumeYes)) {
		std::cout << "Cancelled\n";
		return 0;
	}
	std::vector<std::string> arguments = {"pkgman", "uninstall", "-H", "-y", packageName};
	std::string output;
	std::string error;
	if (!umami::RunPackageCommand(arguments, runner, logger, output, error)) {
		logger.Error(error);
		return 5;
	}
	if (!output.empty())
		std::cout << output;
	state.Remove(identifier);
	if (!state.Save(paths.stateFile, error)) {
		logger.Error("package was removed but state could not be saved: " + error);
		return 2;
	}
	std::cout << packageName << " removed; its settings and user data were left in place\n";
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//

int ShowApplicationInfo(const umami::Catalog& catalog, const umami::InstalledState& state,
	const std::string& identifier, const umami::Logger& logger) {
	const umami::CatalogEntry* entry = catalog.Find(identifier);
	if (entry == nullptr) {
		logger.Error("catalog has no application named '" + identifier + "'");
		return 1;
	}

	std::cout << "ID:            " << entry->id << '\n'
		<< "Name:          " << entry->name << '\n'
		<< "Description:   " << entry->description << '\n'
		<< "Provider:      " << entry->provider << '\n'
		<< "Repository:    " << entry->repository << '\n'
		<< "Package:       " << entry->packageName << '\n'
		<< "Channel:       " << entry->channel << '\n';
	const umami::InstalledEntry* installed = state.Find(entry->id);
	if (installed == nullptr) {
		std::cout << "Tracked:       no\n";
	} else {
		std::cout << "Tracked:       yes\n"
			<< "Version:       " << installed->packageVersion << '\n'
			<< "Release:       " << installed->releaseTag << '\n'
			<< "Scope:         " << installed->scope << '\n'
			<< "Installed at:  " << installed->installedAt << '\n';
	}
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}

int main(int argumentCount, char** arguments) {
	CommandLine commandLine;
	int exitCode = 0;
	if (!ParseCommandLine(argumentCount, arguments, commandLine, exitCode))
		return exitCode;

	umami::Logger logger(commandLine.verbose);
	if (!ValidateCommand(commandLine, logger, exitCode))
		return exitCode;

	umami::AppPaths paths;
	std::string error;
	if (!umami::AppPaths::FromEnvironment(paths, error) || !paths.EnsureDirectories(error)) {
		logger.Error(error);
		return 2;
	}
	if (!commandLine.catalog.empty())
		paths.catalogFile = commandLine.catalog;
	logger.Info("catalog: " + paths.catalogFile.string());
	logger.Info("state: " + paths.stateFile.string());
	if (commandLine.command == "refresh") {
		umami::ProcessRunner runner;
		logger.Info("catalog source: " + std::string(umami::kDefaultCatalogUrl));
		if (!umami::RefreshCatalog(paths.catalogFile, runner, logger, error)) {
			logger.Error(error);
			return 3;
		}
		std::cout << "Catalog refreshed from " << umami::kDefaultCatalogUrl << '\n';
		return 0;
	}
	if (commandLine.command == "recipe") {
		umami::ProcessRunner runner;
		const std::string& action = commandLine.operands[0];
		const std::string& target = commandLine.operands[1];
		std::filesystem::path recipePath;
		umami::RecipeProposal proposal;
		if (action == "add") {
			if (!umami::GenerateRecipe(target, paths.recipeDirectory, paths.cacheDirectory,
				runner, logger, recipePath, proposal, error)) {
				logger.Error(error);
				return 3;
			}
			std::cout << "Recipe proposal written to " << recipePath << "\n\n"
				<< proposal.contents << '\n';
			if (!Confirm("Commit this recipe to umami-cookbook now?", commandLine.assumeYes)) {
				std::cout << "Review or edit it, then run: umami recipe publish " << proposal.app.id << '\n';
				return 0;
			}
			return PublishRecipeProposal(recipePath, paths, runner, logger);
		}

		if (!std::regex_match(target, std::regex("^[a-z0-9][a-z0-9._-]*$"))) {
			logger.Error("recipe id contains unsupported characters");
			return 1;
		}
		recipePath = paths.recipeDirectory / (target + ".json");
		if (!umami::LoadRecipe(recipePath, proposal, error)) {
			logger.Error(error);
			return 2;
		}
		std::cout << "Recipe to publish from " << recipePath << "\n\n" << proposal.contents << '\n';
		if (!Confirm("Commit this recipe to umami-cookbook?", commandLine.assumeYes)) {
			std::cout << "Cancelled\n";
			return 0;
		}
		return PublishRecipeProposal(recipePath, paths, runner, logger);
	}

	std::error_code catalogStatus;
	bool catalogExists = std::filesystem::exists(paths.catalogFile, catalogStatus);
	if (catalogStatus) {
		logger.Error("could not inspect catalog '" + paths.catalogFile.string() + "': " + catalogStatus.message());
		return 2;
	}
	if (!catalogExists) {
		logger.Error("catalog not found at '" + paths.catalogFile.string()
			+ "'; run 'umami refresh', copy catalog.example.json there, or use --catalog <file>");
		return 2;
	}

	umami::Catalog catalog;
	if (!catalog.Load(paths.catalogFile, error)) {
		logger.Error(error);
		return 2;
	}
	umami::InstalledState state;
	if (!state.Load(paths.stateFile, error)) {
		logger.Error(error);
		return 2;
	}

	umami::ProcessRunner runner;
	if (commandLine.command == "list") {
		std::string catalogSource = commandLine.catalog.empty()
			? umami::kDefaultCatalogUrl : commandLine.catalog.string();
		return ListApplications(catalog, state, commandLine.installedOnly, catalogSource);
	}
	if (commandLine.command == "info")
		return ShowApplicationInfo(catalog, state, commandLine.operands.front(), logger);
	if (commandLine.command == "remove")
		return RemoveApplication(commandLine.operands.front(), paths, state, runner, logger, commandLine.assumeYes);
	if (commandLine.command == "install") {
		std::string identifier;
		std::string tag;
		if (!SplitInstallTarget(commandLine.operands.front(), identifier, tag)) {
			logger.Error("usage: umami install <id>[@tag]");
			return 1;
		}
		const umami::CatalogEntry* entry = catalog.Find(identifier);
		if (entry == nullptr) {
			logger.Error("catalog has no application named '" + identifier + "'");
			return 1;
		}
		return InstallApplication(*entry, tag, paths, state, runner, logger, commandLine.assumeYes, false);
	}

	if (!commandLine.operands.empty()) {
		const std::string& identifier = commandLine.operands.front();
		const umami::CatalogEntry* entry = catalog.Find(identifier);
		if (entry == nullptr) {
			logger.Error("catalog has no application named '" + identifier + "'");
			return 1;
		}
		return InstallApplication(*entry, "", paths, state, runner, logger, commandLine.assumeYes, true);
	}
	if (state.Entries().empty()) {
		std::cout << "No tracked applications to update\n";
		return 0;
	}
	for (const auto& installed : state.Entries()) {
		const umami::CatalogEntry* entry = catalog.Find(installed.first);
		if (entry == nullptr) {
			logger.Error("installed application '" + installed.first + "' is no longer in the catalog");
			continue;
		}
		int result = InstallApplication(*entry, "", paths, state, runner, logger, commandLine.assumeYes, true);
		if (result != 0)
			return result;
	}
	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------------//
