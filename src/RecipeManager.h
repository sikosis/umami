#ifndef UMAMI_RECIPE_MANAGER_H
#define UMAMI_RECIPE_MANAGER_H

#include "Catalog.h"

#include <filesystem>
#include <string>

namespace umami {

class Logger;
class ProcessRunner;

struct RecipeProposal {
	CatalogEntry app;
	std::string contents;
};

bool GenerateRecipe(const std::string& repository, const std::filesystem::path& recipeDirectory,
	const std::filesystem::path& cacheDirectory, const ProcessRunner& runner, const Logger& logger,
	std::filesystem::path& recipePath, RecipeProposal& proposal, std::string& error);
bool LoadRecipe(const std::filesystem::path& recipePath, RecipeProposal& proposal, std::string& error);
bool PublishRecipe(const std::filesystem::path& recipePath, const std::string& token,
	const std::filesystem::path& cacheDirectory, const ProcessRunner& runner, const Logger& logger,
	std::string& error);

}

#endif
