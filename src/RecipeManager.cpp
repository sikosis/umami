#include "RecipeManager.h"

#include "JsonSupport.h"
#include "Logger.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace umami {

namespace {

std::string EscapeJson(const std::string& value) {
	std::string escaped;
	for (char character : value) {
		switch (character) {
			case '"': escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\b': escaped += "\\b"; break;
			case '\f': escaped += "\\f"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default: escaped += character;
		}
	}
	return escaped;
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::string LowercaseIdentifier(const std::string& name) {
	std::string identifier;
	for (unsigned char character : name) {
		if (std::isalnum(character) || character == '.' || character == '_' || character == '-')
			identifier += static_cast<char>(std::tolower(character));
		else if (!identifier.empty() && identifier.back() != '-')
			identifier += '-';
	}
	while (!identifier.empty() && identifier.back() == '-')
		identifier.pop_back();
	return identifier;
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::string SerialiseRecipe(const CatalogEntry& app) {
	std::ostringstream output;
	output << "{\n"
		<< "  \"$schema\": \"../schema/recipe-v1.schema.json\",\n"
		<< "  \"version\": 1,\n"
		<< "  \"app\": {\n"
		<< "    \"id\": \"" << EscapeJson(app.id) << "\",\n"
		<< "    \"name\": \"" << EscapeJson(app.name) << "\",\n"
		<< "    \"description\": \"" << EscapeJson(app.description) << "\",\n"
		<< "    \"provider\": \"github\",\n"
		<< "    \"repo\": \"" << EscapeJson(app.repository) << "\",\n";
	if (!app.releaseTagPattern.empty())
		output << "    \"release_tag_pattern\": \"" << EscapeJson(app.releaseTagPattern) << "\",\n";
	output
		<< "    \"asset_pattern\": \"" << EscapeJson(app.assetPattern) << "\",\n"
		<< "    \"package_name\": \"" << EscapeJson(app.packageName) << "\",\n"
		<< "    \"channel\": \"" << EscapeJson(app.channel) << "\"\n"
		<< "  }\n"
		<< "}\n";
	return output.str();
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ReadFile(const std::filesystem::path& path, std::string& contents, std::string& error) {
	std::ifstream input(path);
	if (!input) {
		error = "could not open '" + path.string() + "'";
		return false;
	}
	std::ostringstream output;
	output << input.rdbuf();
	if (input.bad()) {
		error = "could not read '" + path.string() + "'";
		return false;
	}
	contents = output.str();
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool WriteFile(const std::filesystem::path& path, const std::string& contents, std::string& error) {
	std::ofstream output(path, std::ios::trunc);
	if (!output) {
		error = "could not write '" + path.string() + "'";
		return false;
	}
	output << contents;
	if (!output) {
		error = "could not finish writing '" + path.string() + "'";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool WritePrivateFile(const std::filesystem::path& path, const std::string& contents, std::string& error) {
	int descriptor = open(path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0) {
		error = "could not create private authentication file";
		return false;
	}
	if (fchmod(descriptor, 0600) != 0) {
		close(descriptor);
		std::error_code status;
		std::filesystem::remove(path, status);
		error = "could not protect private authentication file";
		return false;
	}
	std::size_t written = 0;
	while (written < contents.size()) {
		ssize_t count = write(descriptor, contents.data() + written, contents.size() - written);
		if (count <= 0) {
			close(descriptor);
			std::error_code status;
			std::filesystem::remove(path, status);
			error = "could not write private authentication file";
			return false;
		}
		written += static_cast<std::size_t>(count);
	}
	if (close(descriptor) != 0) {
		std::error_code status;
		std::filesystem::remove(path, status);
		error = "could not close private authentication file";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

std::string Base64Encode(const std::string& value) {
	const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string encoded;
	for (std::size_t index = 0; index < value.size(); index += 3) {
		unsigned int block = static_cast<unsigned char>(value[index]) << 16;
		bool hasSecond = index + 1 < value.size();
		bool hasThird = index + 2 < value.size();
		if (hasSecond)
			block |= static_cast<unsigned char>(value[index + 1]) << 8;
		if (hasThird)
			block |= static_cast<unsigned char>(value[index + 2]);
		encoded += alphabet[(block >> 18) & 0x3f];
		encoded += alphabet[(block >> 12) & 0x3f];
		encoded += hasSecond ? alphabet[(block >> 6) & 0x3f] : '=';
		encoded += hasThird ? alphabet[block & 0x3f] : '=';
	}
	return encoded;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool ValidateApp(const JsonValue& app, CatalogEntry& entry, std::string& error) {
	if (app.ValueType() != JsonValue::Type::Object) {
		error = "recipe field 'app' must be an object";
		return false;
	}
	if (!ReadRequiredString(app, "id", entry.id, error)
		|| !ReadRequiredString(app, "name", entry.name, error)
		|| !ReadRequiredString(app, "description", entry.description, error)
		|| !ReadRequiredString(app, "provider", entry.provider, error)
		|| !ReadRequiredString(app, "repo", entry.repository, error)
		|| !ReadOptionalString(app, "release_tag_pattern", entry.releaseTagPattern, error)
		|| !ReadRequiredString(app, "asset_pattern", entry.assetPattern, error)
		|| !ReadRequiredString(app, "package_name", entry.packageName, error)
		|| !ReadRequiredString(app, "channel", entry.channel, error)
		|| !ReadOptionalString(app, "checksum_asset", entry.checksumAsset, error))
		return false;
	if (entry.provider != "github") {
		error = "recipe provider must be 'github'";
		return false;
	}
	if (!std::regex_match(entry.id, std::regex("^[a-z0-9][a-z0-9._-]*$"))) {
		error = "recipe id contains unsupported characters";
		return false;
	}
	if (!std::regex_match(entry.repository, std::regex("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"))) {
		error = "recipe repo must use the owner/repository form";
		return false;
	}
	if (!entry.releaseTagPattern.empty()) {
		try {
			std::regex matcher(entry.releaseTagPattern);
			(void)matcher;
		} catch (const std::regex_error&) {
			error = "recipe release_tag_pattern is not a valid regular expression";
			return false;
		}
	}
	if (!std::regex_match(entry.packageName, std::regex("^[A-Za-z0-9][A-Za-z0-9+_.-]*$"))) {
		error = "recipe package_name contains unsupported characters";
		return false;
	}
	if (entry.assetPattern.find("{arch}") == std::string::npos) {
		error = "recipe asset_pattern must contain {arch}";
		return false;
	}
	try {
		std::string testPattern = entry.assetPattern;
		testPattern.replace(testPattern.find("{arch}"), 6, "x86_64");
		std::regex matcher(testPattern);
		(void)matcher;
	} catch (const std::regex_error&) {
		error = "recipe asset_pattern is not a valid regular expression";
		return false;
	}
	if (entry.channel != "stable" && entry.channel != "prerelease") {
		error = "recipe channel must be 'stable' or 'prerelease'";
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}

bool GenerateRecipe(const std::string& repository, const std::filesystem::path& recipeDirectory,
	const std::filesystem::path& cacheDirectory, const ProcessRunner& runner, const Logger& logger,
	std::filesystem::path& recipePath, RecipeProposal& proposal, std::string& error) {
	if (!std::regex_match(repository, std::regex("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"))) {
		error = "repository must use the owner/repository form";
		return false;
	}
	std::filesystem::path responsePath = cacheDirectory / "recipe-repository.json";
	std::vector<std::string> arguments = {
		"curl", "--fail", "--silent", "--show-error", "--connect-timeout", "10", "--max-time", "60",
		"--header", "Accept: application/vnd.github+json", "--header", "User-Agent: umami",
		"--output", responsePath.string(), "https://api.github.com/repos/" + repository
	};
	logger.Command(arguments);
	ProcessResult result = runner.Run(arguments);
	if (!result.started) {
		error = result.error;
		return false;
	}
	if (result.exitCode != 0) {
		error = "could not read GitHub repository metadata";
		if (!result.output.empty())
			error += ": " + result.output;
		return false;
	}
	JsonValue root;
	if (!LoadJsonFile(responsePath, root, error))
		return false;
	if (root.ValueType() != JsonValue::Type::Object) {
		error = "GitHub repository response must be an object";
		return false;
	}
	std::string repositoryName;
	if (!ReadRequiredString(root, "name", repositoryName, error))
		return false;
	std::string description;
	const JsonValue* descriptionValue = root.Find("description");
	if (descriptionValue != nullptr && descriptionValue->ValueType() == JsonValue::Type::String)
		description = descriptionValue->StringValue();
	if (description.empty())
		description = "A Haiku application from " + repository + ".";

	proposal.app.id = LowercaseIdentifier(repositoryName);
	if (proposal.app.id.empty()) {
		error = "repository name cannot be converted into a recipe id";
		return false;
	}
	proposal.app.name = repositoryName;
	proposal.app.description = description;
	proposal.app.provider = "github";
	proposal.app.repository = repository;
	proposal.app.assetPattern = "^" + proposal.app.id + "-.*-{arch}\\.hpkg$";
	proposal.app.packageName = proposal.app.id;
	proposal.app.channel = "stable";
	proposal.contents = SerialiseRecipe(proposal.app);
	recipePath = recipeDirectory / (proposal.app.id + ".json");
	std::error_code status;
	bool exists = std::filesystem::exists(recipePath, status);
	if (status) {
		error = "could not inspect recipe path: " + status.message();
		return false;
	}
	if (exists) {
		error = "recipe proposal already exists at '" + recipePath.string() + "'";
		return false;
	}
	return WriteFile(recipePath, proposal.contents, error);
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool LoadRecipe(const std::filesystem::path& recipePath, RecipeProposal& proposal, std::string& error) {
	JsonValue root;
	if (!LoadJsonFile(recipePath, root, error))
		return false;
	if (root.ValueType() != JsonValue::Type::Object) {
		error = "recipe root must be an object";
		return false;
	}
	const JsonValue* version = root.Find("version");
	if (version == nullptr || version->ValueType() != JsonValue::Type::Integer || version->IntegerValue() != 1) {
		error = "recipe version must be 1";
		return false;
	}
	const JsonValue* app = root.Find("app");
	if (app == nullptr) {
		error = "recipe field 'app' must be an object";
		return false;
	}
	if (!ValidateApp(*app, proposal.app, error))
		return false;
	return ReadFile(recipePath, proposal.contents, error);
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool PublishRecipe(const std::filesystem::path& recipePath, const std::string& token,
	const std::filesystem::path& cacheDirectory, const ProcessRunner& runner, const Logger& logger,
	std::string& error) {
	RecipeProposal proposal;
	if (!LoadRecipe(recipePath, proposal, error))
		return false;
	if (token.empty() || std::any_of(token.begin(), token.end(), [](unsigned char character) {
		return std::isspace(character) != 0;
	})) {
		error = "GitHub token must be non-empty and contain no whitespace";
		return false;
	}

	std::filesystem::path authenticationPath = cacheDirectory / ".github-auth";
	std::filesystem::path payloadPath = cacheDirectory / "recipe-publish.json";
	std::string authentication = "machine api.github.com\nlogin sikosis\npassword " + token + "\n";
	if (!WritePrivateFile(authenticationPath, authentication, error)) {
		std::fill(authentication.begin(), authentication.end(), '\0');
		return false;
	}
	std::fill(authentication.begin(), authentication.end(), '\0');
	std::string payload = "{\"message\":\"Add " + EscapeJson(proposal.app.id)
		+ " recipe\",\"content\":\"" + Base64Encode(proposal.contents) + "\"}\n";
	if (!WriteFile(payloadPath, payload, error)) {
		std::error_code status;
		std::filesystem::remove(authenticationPath, status);
		return false;
	}
	std::string url = "https://api.github.com/repos/sikosis/umami-cookbook/contents/recipes/"
		+ proposal.app.id + ".json";
	std::vector<std::string> arguments = {
		"curl", "--fail", "--silent", "--show-error", "--request", "PUT", "--netrc-file",
		authenticationPath.string(), "--header", "Accept: application/vnd.github+json",
		"--header", "Content-Type: application/json", "--header", "User-Agent: umami",
		"--data-binary", "@" + payloadPath.string(), url
	};
	logger.Command(arguments);
	ProcessResult result = runner.Run(arguments);
	std::error_code status;
	std::filesystem::remove(authenticationPath, status);
	std::filesystem::remove(payloadPath, status);
	if (!result.started) {
		error = result.error;
		return false;
	}
	if (result.exitCode != 0) {
		error = "GitHub could not commit the recipe";
		if (!result.output.empty())
			error += ": " + result.output;
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
