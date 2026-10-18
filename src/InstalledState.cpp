#include "InstalledState.h"

#include "JsonSupport.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string EscapeJson(const std::string& value) {
	std::ostringstream output;
	for (unsigned char character : value) {
		switch (character) {
			case '"': output << "\\\""; break;
			case '\\': output << "\\\\"; break;
			case '\b': output << "\\b"; break;
			case '\f': output << "\\f"; break;
			case '\n': output << "\\n"; break;
			case '\r': output << "\\r"; break;
			case '\t': output << "\\t"; break;
			default:
				if (character < 0x20)
					output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character);
				else
					output << static_cast<char>(character);
		}
	}
	return output.str();
}
//---------------------------------------------------------------------------------------------------------------------------------//

}

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

bool InstalledState::Save(const std::filesystem::path& path, std::string& error) const {
	error.clear();
	std::filesystem::path temporaryPath = path;
	temporaryPath += ".tmp";
	std::ofstream output(temporaryPath, std::ios::trunc);
	if (!output) {
		error = "could not write '" + temporaryPath.string() + "'";
		return false;
	}

	output << "{\n  \"version\": 1,\n  \"installed\": {";
	bool first = true;
	for (const auto& stateEntry : fEntries) {
		if (!first)
			output << ',';
		first = false;
		const InstalledEntry& entry = stateEntry.second;
		output << "\n    \"" << EscapeJson(stateEntry.first) << "\": {\n"
			<< "      \"package_name\": \"" << EscapeJson(entry.packageName) << "\",\n"
			<< "      \"package_version\": \"" << EscapeJson(entry.packageVersion) << "\",\n"
			<< "      \"release_tag\": \"" << EscapeJson(entry.releaseTag) << "\",\n"
			<< "      \"asset\": \"" << EscapeJson(entry.asset) << "\",\n"
			<< "      \"sha256\": \"" << EscapeJson(entry.sha256) << "\",\n"
			<< "      \"scope\": \"" << EscapeJson(entry.scope) << "\",\n"
			<< "      \"installed_at\": \"" << EscapeJson(entry.installedAt) << "\"\n"
			<< "    }";
	}
	if (!fEntries.empty())
		output << '\n';
	output << "  }\n}\n";
	output.close();
	if (!output) {
		error = "could not finish writing '" + temporaryPath.string() + "'";
		return false;
	}

	std::error_code status;
	std::filesystem::rename(temporaryPath, path, status);
	if (status) {
		error = "could not replace '" + path.string() + "': " + status.message();
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------------------------------------------------------------//

void InstalledState::Set(const std::string& id, const InstalledEntry& entry) {
	fEntries[id] = entry;
}
//---------------------------------------------------------------------------------------------------------------------------------//

bool InstalledState::Remove(const std::string& id) {
	return fEntries.erase(id) != 0;
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
