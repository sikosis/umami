#include "PackageManager.h"

#include "JsonSupport.h"
#include "Logger.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace umami {

namespace {

std::string Trim(const std::string& value) {
	std::size_t start = 0;
	while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
		++start;
	std::size_t end = value.size();
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
		--end;
	return value.substr(start, end - start);
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::string EncodeUrlComponent(const std::string& value) {
	const char* digits = "0123456789ABCDEF";
	std::string encoded;
	for (unsigned char character : value) {
		if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
			encoded += static_cast<char>(character);
		} else {
			encoded += '%';
			encoded += digits[character >> 4];
			encoded += digits[character & 0x0f];
		}
	}
	return encoded;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool RunCurlToFile(const std::string& url, const std::filesystem::path& destination,
	const ProcessRunner& runner, const Logger& logger, std::string& error) {
	std::vector<std::string> arguments = {
		"curl", "--fail", "--location", "--silent", "--show-error", "--connect-timeout", "10",
		"--max-time", "120", "--header", "Accept: application/vnd.github+json",
		"--header", "User-Agent: umami", "--output", destination.string(), url
	};
	logger.Command(arguments);
	ProcessResult result = runner.Run(arguments);
	if (!result.started) {
		error = result.error;
		return false;
	}
	if (result.exitCode != 0) {
		error = "download failed";
		std::string detail = Trim(result.output);
		if (!detail.empty())
			error += ": " + detail;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const JsonValue* SelectReleaseObject(const JsonValue& root, bool findPrerelease, std::string& error) {
	if (!findPrerelease) {
		if (root.ValueType() != JsonValue::Type::Object)
			error = "GitHub release response must be an object";
		return error.empty() ? &root : nullptr;
	}
	if (root.ValueType() != JsonValue::Type::Array) {
		error = "GitHub prerelease response must be an array";
		return nullptr;
	}
	for (const JsonValue& candidate : root.ArrayValue()) {
		if (candidate.ValueType() != JsonValue::Type::Object)
			continue;
		const JsonValue* prerelease = candidate.Find("prerelease");
		const JsonValue* draft = candidate.Find("draft");
		if (prerelease != nullptr && prerelease->ValueType() == JsonValue::Type::Boolean
			&& prerelease->BooleanValue() && (draft == nullptr || draft->ValueType() != JsonValue::Type::Boolean
				|| !draft->BooleanValue()))
			return &candidate;
	}
	error = "repository has no published prerelease";
	return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ReadAsset(const JsonValue& value, ReleaseAsset& asset, std::string& error) {
	if (value.ValueType() != JsonValue::Type::Object)
		return false;
	return ReadRequiredString(value, "name", asset.name, error)
		&& ReadRequiredString(value, "browser_download_url", asset.downloadUrl, error);
}
//---------------------------------------------------------------------------------------------------------------------------------//

}

bool DetectArchitecture(const ProcessRunner& runner, const Logger& logger,
	std::string& architecture, std::string& error) {
	std::vector<std::string> arguments = {"getarch"};
	logger.Command(arguments);
	ProcessResult result = runner.Run(arguments);
	if (!result.started) {
		error = result.error;
		return false;
	}
	if (result.exitCode != 0) {
		error = "getarch failed: " + Trim(result.output);
		return false;
	}
	architecture = Trim(result.output);
	if (architecture.empty()) {
		error = "getarch returned an empty architecture";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ParseReleaseFile(const std::filesystem::path& path, const CatalogEntry& entry,
	const std::string& architecture, bool findPrerelease, ReleaseInfo& release, std::string& error) {
	error.clear();
	JsonValue root;
	if (!LoadJsonFile(path, root, error))
		return false;
	const JsonValue* releaseObject = SelectReleaseObject(root, findPrerelease, error);
	if (releaseObject == nullptr)
		return false;
	if (!ReadRequiredString(*releaseObject, "tag_name", release.tag, error)) {
		error = "GitHub release: " + error;
		return false;
	}
	const JsonValue* assets = releaseObject->Find("assets");
	if (assets == nullptr || assets->ValueType() != JsonValue::Type::Array) {
		error = "GitHub release field 'assets' must be an array";
		return false;
	}
	std::string pattern = entry.assetPattern;
	std::size_t position = 0;
	while ((position = pattern.find("{arch}", position)) != std::string::npos) {
		pattern.replace(position, 6, architecture);
		position += architecture.size();
	}
	std::regex matcher(pattern);
	for (const JsonValue& value : assets->ArrayValue()) {
		ReleaseAsset asset;
		std::string assetError;
		if (!ReadAsset(value, asset, assetError))
			continue;
		if (std::regex_match(asset.name, matcher)) {
			release.asset = asset;
			return true;
		}
	}
	error = "release '" + release.tag + "' has no asset matching architecture '" + architecture + "'";
	return false;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ResolveRelease(const CatalogEntry& entry, const std::string& requestedTag,
	const std::string& architecture, const std::filesystem::path& cacheDirectory,
	const ProcessRunner& runner, const Logger& logger, ReleaseInfo& release, std::string& error) {
	bool findPrerelease = requestedTag.empty() && entry.channel == "prerelease";
	std::string url = "https://api.github.com/repos/" + entry.repository + "/releases";
	if (!requestedTag.empty())
		url += "/tags/" + EncodeUrlComponent(requestedTag);
	else if (findPrerelease)
		url += "?per_page=30";
	else
		url += "/latest";
	std::filesystem::path responseFile = cacheDirectory / (entry.id + "-release.json");
	if (!RunCurlToFile(url, responseFile, runner, logger, error)) {
		if (error.find("404") != std::string::npos) {
			if (!requestedTag.empty())
				error = "repository '" + entry.repository + "' has no release tagged '" + requestedTag + "'";
			else if (findPrerelease)
				error = "repository '" + entry.repository + "' has no published prerelease";
			else
				error = "repository '" + entry.repository + "' has no published stable release";
		}
		return false;
	}
	return ParseReleaseFile(responseFile, entry, architecture, findPrerelease, release, error);
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool DownloadReleaseAsset(const ReleaseAsset& asset, const std::filesystem::path& cacheDirectory,
	const ProcessRunner& runner, const Logger& logger, std::filesystem::path& packageFile, std::string& error) {
	if (asset.name.empty() || asset.name == "." || asset.name == ".."
		|| asset.name.find('/') != std::string::npos || asset.name.find('\\') != std::string::npos) {
		error = "release asset has an unsafe filename";
		return false;
	}
	if (asset.downloadUrl.rfind("https://", 0) != 0) {
		error = "release asset URL must use HTTPS";
		return false;
	}
	packageFile = cacheDirectory / asset.name;
	std::filesystem::path temporaryFile = packageFile;
	temporaryFile += ".download";
	std::error_code status;
	std::filesystem::remove(temporaryFile, status);
	if (status) {
		error = "could not remove stale download: " + status.message();
		return false;
	}
	if (!RunCurlToFile(asset.downloadUrl, temporaryFile, runner, logger, error))
		return false;
	std::filesystem::rename(temporaryFile, packageFile, status);
	if (status) {
		error = "could not store downloaded package: " + status.message();
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ParsePackageInfo(const std::string& output, PackageDetails& details, std::string& error) {
	details = PackageDetails();
	std::istringstream lines(output);
	std::string line;
	while (std::getline(lines, line)) {
		std::size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = Trim(line.substr(0, colon));
		std::string value = Trim(line.substr(colon + 1));
		if (key == "name")
			details.name = value;
		else if (key == "version")
			details.version = value;
		else if (key == "architecture")
			details.architecture = value;
	}
	if (details.name.empty() || details.version.empty() || details.architecture.empty()) {
		error = "package metadata did not report name, version and architecture";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool InspectPackage(const std::filesystem::path& packageFile, const ProcessRunner& runner,
	const Logger& logger, PackageDetails& details, std::string& error) {
	std::vector<std::string> arguments = {"package", "list", "-i", packageFile.string()};
	std::string output;
	if (!RunPackageCommand(arguments, runner, logger, output, error))
		return false;
	if (!ParsePackageInfo(output, details, error)) {
		std::string detail = Trim(output);
		if (!detail.empty())
			error += ": " + detail;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ComputeSha256(const std::filesystem::path& packageFile, const ProcessRunner& runner,
	const Logger& logger, std::string& checksum, std::string& error) {
	std::vector<std::string> arguments = {"sha256sum", packageFile.string()};
	std::string output;
	if (!RunPackageCommand(arguments, runner, logger, output, error))
		return false;
	std::istringstream values(output);
	values >> checksum;
	if (checksum.size() != 64 || !std::all_of(checksum.begin(), checksum.end(), [](unsigned char character) {
		return std::isxdigit(character) != 0;
	})) {
		error = "sha256sum returned an invalid checksum";
		return false;
	}
	std::transform(checksum.begin(), checksum.end(), checksum.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool IsPackageInstalled(const std::string& packageName, const ProcessRunner& runner,
	const Logger& logger, bool& installed, std::string& error) {
	std::vector<std::string> arguments = {"pkgman", "search", "-i", "-a", packageName};
	std::string output;
	if (!RunPackageCommand(arguments, runner, logger, output, error))
		return false;
	installed = false;
	std::istringstream words(output);
	std::string word;
	while (words >> word) {
		if (word == packageName) {
			installed = true;
			break;
		}
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool RunPackageCommand(const std::vector<std::string>& arguments, const ProcessRunner& runner,
	const Logger& logger, std::string& output, std::string& error) {
	logger.Command(arguments);
	ProcessResult result = runner.Run(arguments);
	output = result.output;
	if (!result.started) {
		error = result.error;
		return false;
	}
	if (result.exitCode != 0) {
		error = "command '" + arguments.front() + "' failed";
		std::string detail = Trim(result.output);
		if (!detail.empty())
			error += ": " + detail;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
