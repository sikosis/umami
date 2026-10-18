#include "Logger.h"

#include <iostream>

namespace umami {

namespace {

std::string QuoteForDisplay(const std::string& argument) {
	if (argument.find_first_of(" \t\"'") == std::string::npos)
		return argument;

	std::string quoted = "'";
	for (char character : argument) {
		if (character == '\'')
			quoted += "'\\''";
		else
			quoted += character;
	}
	quoted += "'";
	return quoted;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}

Logger::Logger(bool verbose)
	: fVerbose(verbose) {
}
//---------------------------------------------------------------------------------------------------------------------------------//

void Logger::Error(const std::string& message) const {
	std::cerr << "umami: " << message << '\n';
}
//---------------------------------------------------------------------------------------------------------------------------------//

void Logger::Info(const std::string& message) const {
	if (fVerbose)
		std::cerr << message << '\n';
}
//---------------------------------------------------------------------------------------------------------------------------------//

void Logger::Command(const std::vector<std::string>& arguments) const {
	if (!fVerbose)
		return;

	std::cerr << "+";
	for (const std::string& argument : arguments)
		std::cerr << " " << QuoteForDisplay(argument);
	std::cerr << '\n';
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
