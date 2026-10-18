#ifndef UMAMI_INSTALLED_STATE_H
#define UMAMI_INSTALLED_STATE_H

#include <filesystem>
#include <map>
#include <string>

namespace umami {

struct InstalledEntry {
	std::string packageName;
	std::string packageVersion;
	std::string releaseTag;
	std::string asset;
	std::string sha256;
	std::string scope;
	std::string installedAt;
};

class InstalledState {
public:
	bool Load(const std::filesystem::path& path, std::string& error);
	bool Save(const std::filesystem::path& path, std::string& error) const;
	void Set(const std::string& id, const InstalledEntry& entry);
	bool Remove(const std::string& id);
	const InstalledEntry* Find(const std::string& id) const;
	const std::map<std::string, InstalledEntry>& Entries() const;

private:
	std::map<std::string, InstalledEntry> fEntries;
};

}

#endif
