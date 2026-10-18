#include "Catalog.h"
#include "CatalogRefresh.h"
#include "InstalledState.h"
#include "Logger.h"
#include "Paths.h"
#include "ProcessRunner.h"
#include "Version.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct CommandLine {
	bool verbose = false;
	bool installedOnly = false;
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
		<< "  install <id>[@tag]       Install an application (planned)\n"
		<< "  remove <id>              Remove an application (planned)\n"
		<< "  update [<id>]            Update installed applications (planned)\n"
		<< "  refresh                  Download the latest cookbook catalog\n"
		<< "  doctor                   Check tools, cache and state (planned)\n"
		<< "\n"
		<< "Common options:\n"
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
	if (commandLine.command != "list" && commandLine.command != "info" && commandLine.command != "refresh") {
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
	if (commandLine.command != "list" && commandLine.installedOnly) {
		logger.Error("--installed can only be used with list");
		exitCode = 1;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

int ListApplications(const umami::Catalog& catalog, const umami::InstalledState& state, bool installedOnly) {
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

	if (commandLine.command == "list") {
		return ListApplications(catalog, state, commandLine.installedOnly);
	}
	return ShowApplicationInfo(catalog, state, commandLine.operands.front(), logger);
}
//---------------------------------------------------------------------------------------------------------------------------------//
