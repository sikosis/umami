#ifndef UMAMI_PROCESS_RUNNER_H
#define UMAMI_PROCESS_RUNNER_H

#include <string>
#include <vector>

namespace umami {

struct ProcessResult {
	bool started = false;
	int exitCode = -1;
	std::string output;
	std::string error;
};

class ProcessRunner {
public:
	ProcessResult Run(const std::vector<std::string>& arguments) const;
};

}

#endif
