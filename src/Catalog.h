#ifndef UMAMI_CATALOG_H
#define UMAMI_CATALOG_H

#include <filesystem>
#include <string>
#include <vector>

namespace umami {

struct CatalogEntry {
	std::string id;
	std::string name;
	std::string description;
	std::string provider;
	std::string repository;
	std::string assetPattern;
	std::string packageName;
	std::string channel;
	std::string checksumAsset;
};

class Catalog {
public:
	bool Load(const std::filesystem::path& path, std::string& error);
	const CatalogEntry* Find(const std::string& id) const;
	const std::vector<CatalogEntry>& Entries() const;

private:
	std::vector<CatalogEntry> fEntries;
};

}

#endif
