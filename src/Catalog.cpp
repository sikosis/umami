#include "Catalog.h"

#include "JsonSupport.h"

#include <regex>
#include <set>

namespace umami {

bool Catalog::Load(const std::filesystem::path& path, std::string& error) {
	error.clear();
	JsonValue root;
	if (!LoadJsonFile(path, root, error))
		return false;

	bool success = false;
	do {
		if (root.ValueType() != JsonValue::Type::Object) {
			error = "catalog root must be an object";
			break;
		}

		const JsonValue* version = root.Find("version");
		if (version == nullptr || version->ValueType() != JsonValue::Type::Integer || version->IntegerValue() != 1) {
			error = "catalog version must be 1";
			break;
		}

		const JsonValue* apps = root.Find("apps");
		if (apps == nullptr || apps->ValueType() != JsonValue::Type::Array) {
			error = "catalog field 'apps' must be an array";
			break;
		}

		std::vector<CatalogEntry> entries;
		std::set<std::string> identifiers;
		for (std::size_t index = 0; index < apps->ArrayValue().size(); ++index) {
			const JsonValue& app = apps->ArrayValue()[index];
			if (app.ValueType() != JsonValue::Type::Object) {
				error = "catalog app " + std::to_string(index + 1) + " must be an object";
				break;
			}

			CatalogEntry entry;
			if (!ReadRequiredString(app, "id", entry.id, error)
				|| !ReadRequiredString(app, "name", entry.name, error)
				|| !ReadRequiredString(app, "description", entry.description, error)
				|| !ReadRequiredString(app, "provider", entry.provider, error)
				|| !ReadRequiredString(app, "repo", entry.repository, error)
				|| !ReadRequiredString(app, "asset_pattern", entry.assetPattern, error)
				|| !ReadRequiredString(app, "package_name", entry.packageName, error)
				|| !ReadRequiredString(app, "channel", entry.channel, error)
				|| !ReadOptionalString(app, "checksum_asset", entry.checksumAsset, error)) {
				error = "catalog app " + std::to_string(index + 1) + ": " + error;
				break;
			}
			if (entry.provider != "github") {
				error = "catalog app '" + entry.id + "': provider '" + entry.provider + "' is not supported yet";
				break;
			}
			if (!std::regex_match(entry.id, std::regex("^[a-z0-9][a-z0-9._-]*$"))) {
				error = "catalog app " + std::to_string(index + 1) + ": id contains unsupported characters";
				break;
			}
			if (!std::regex_match(entry.repository, std::regex("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"))) {
				error = "catalog app '" + entry.id + "': repo must use the owner/repository form";
				break;
			}
			if (!std::regex_match(entry.packageName, std::regex("^[A-Za-z0-9][A-Za-z0-9+_.-]*$"))) {
				error = "catalog app '" + entry.id + "': package_name contains unsupported characters";
				break;
			}
			std::size_t architecturePlaceholder = entry.assetPattern.find("{arch}");
			if (architecturePlaceholder == std::string::npos) {
				error = "catalog app '" + entry.id + "': asset_pattern must contain {arch}";
				break;
			}
			std::string testPattern = entry.assetPattern;
			testPattern.replace(architecturePlaceholder, 6, "x86_64");
			try {
				std::regex compiledPattern(testPattern);
				(void)compiledPattern;
			} catch (const std::regex_error&) {
				error = "catalog app '" + entry.id + "': asset_pattern is not a valid regular expression";
				break;
			}
			if (entry.channel != "stable" && entry.channel != "prerelease") {
				error = "catalog app '" + entry.id + "': channel must be 'stable' or 'prerelease'";
				break;
			}
			if (!identifiers.insert(entry.id).second) {
				error = "catalog contains duplicate id '" + entry.id + "'";
				break;
			}
			entries.push_back(entry);
		}
		if (!error.empty())
			break;
		fEntries = std::move(entries);
		success = true;
	} while (false);

	return success;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const CatalogEntry* Catalog::Find(const std::string& id) const {
	for (const CatalogEntry& entry : fEntries) {
		if (entry.id == id)
			return &entry;
	}
	return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const std::vector<CatalogEntry>& Catalog::Entries() const {
	return fEntries;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
