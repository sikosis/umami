#include "InstalledState.h"

#include "JsonSupport.h"

namespace umami {

bool InstalledState::Load(const std::filesystem::path& path, std::string& error) {
	error.clear();
	std::error_code fileStatus;
	if (!std::filesystem::exists(path, fileStatus)) {
		if (fileStatus) {
			error = "could not inspect '" + path.string() + "': " + fileStatus.message();
			return false;
		}
		fEntries.clear();
		return true;
	}

	JsonValue root;
	if (!LoadJsonFile(path, root, error))
		return false;

	bool success = false;
	do {
		if (root.ValueType() != JsonValue::Type::Object) {
			error = "installed state root must be an object";
			break;
		}
		const JsonValue* version = root.Find("version");
		if (version == nullptr || version->ValueType() != JsonValue::Type::Integer || version->IntegerValue() != 1) {
			error = "installed state version must be 1";
			break;
		}
		const JsonValue* installed = root.Find("installed");
		if (installed == nullptr || installed->ValueType() != JsonValue::Type::Object) {
			error = "installed state field 'installed' must be an object";
			break;
		}

		std::map<std::string, InstalledEntry> entries;
		for (const auto& stateEntry : installed->ObjectValue()) {
			const std::string& identifier = stateEntry.first;
			const JsonValue& value = stateEntry.second;
			if (value.ValueType() != JsonValue::Type::Object) {
				error = "installed entry '" + identifier + "' must be an object";
				break;
			}
			InstalledEntry entry;
			if (!ReadRequiredString(value, "package_name", entry.packageName, error)
				|| !ReadRequiredString(value, "package_version", entry.packageVersion, error)
				|| !ReadRequiredString(value, "release_tag", entry.releaseTag, error)
				|| !ReadRequiredString(value, "asset", entry.asset, error)
				|| !ReadRequiredString(value, "sha256", entry.sha256, error)
				|| !ReadRequiredString(value, "scope", entry.scope, error)
				|| !ReadRequiredString(value, "installed_at", entry.installedAt, error)) {
				error = "installed entry '" + identifier + "': " + error;
				break;
			}
			if (entry.scope != "user" && entry.scope != "system") {
				error = "installed entry '" + identifier + "': scope must be 'user' or 'system'";
				break;
			}
			entries.emplace(identifier, entry);
		}
		if (!error.empty())
			break;
		fEntries = std::move(entries);
		success = true;
	} while (false);

	return success;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const InstalledEntry* InstalledState::Find(const std::string& id) const {
	auto entry = fEntries.find(id);
	return entry == fEntries.end() ? nullptr : &entry->second;
}
//---------------------------------------------------------------------------------------------------------------------------------//

const std::map<std::string, InstalledEntry>& InstalledState::Entries() const {
	return fEntries;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
