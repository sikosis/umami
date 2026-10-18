#ifndef UMAMI_LOGGER_H
#define UMAMI_LOGGER_H

#include <string>
#include <vector>

namespace umami {

class Logger {
public:
	explicit Logger(bool verbose);

	void Error(const std::string& message) const;
	void Info(const std::string& message) const;
	void Command(const std::vector<std::string>& arguments) const;

private:
	bool fVerbose;
};

}

#endif
