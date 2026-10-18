#include "ProcessRunner.h"

#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace umami {

ProcessResult ProcessRunner::Run(const std::vector<std::string>& arguments) const {
	ProcessResult result;
	if (arguments.empty() || arguments.front().empty()) {
		result.error = "cannot run an empty command";
		return result;
	}

	int outputPipe[2];
	if (pipe(outputPipe) != 0) {
		result.error = std::string("could not create output pipe: ") + std::strerror(errno);
		return result;
	}

	posix_spawn_file_actions_t actions;
	int actionStatus = posix_spawn_file_actions_init(&actions);
	if (actionStatus != 0) {
		close(outputPipe[0]);
		close(outputPipe[1]);
		result.error = std::string("could not initialise process actions: ") + std::strerror(actionStatus);
		return result;
	}

	actionStatus = posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
	if (actionStatus == 0)
		actionStatus = posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDERR_FILENO);
	if (actionStatus == 0)
		actionStatus = posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
	if (actionStatus == 0)
		actionStatus = posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
	if (actionStatus != 0) {
		posix_spawn_file_actions_destroy(&actions);
		close(outputPipe[0]);
		close(outputPipe[1]);
		result.error = std::string("could not configure process output: ") + std::strerror(actionStatus);
		return result;
	}

	std::vector<char*> processArguments;
	processArguments.reserve(arguments.size() + 1);
	for (const std::string& argument : arguments)
		processArguments.push_back(const_cast<char*>(argument.c_str()));
	processArguments.push_back(nullptr);

	pid_t processId = 0;
	int spawnStatus = posix_spawnp(&processId, processArguments.front(), &actions, nullptr,
		processArguments.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	close(outputPipe[1]);
	if (spawnStatus != 0) {
		close(outputPipe[0]);
		result.error = std::string("could not start '") + arguments.front() + "': " + std::strerror(spawnStatus);
		return result;
	}

	result.started = true;
	char buffer[4096];
	while (true) {
		ssize_t bytesRead = read(outputPipe[0], buffer, sizeof(buffer));
		if (bytesRead > 0) {
			result.output.append(buffer, static_cast<std::size_t>(bytesRead));
			continue;
		}
		if (bytesRead < 0 && errno == EINTR)
			continue;
		if (bytesRead < 0)
			result.error = std::string("could not read process output: ") + std::strerror(errno);
		break;
	}
	close(outputPipe[0]);

	int processStatus = 0;
	while (waitpid(processId, &processStatus, 0) < 0) {
		if (errno == EINTR)
			continue;
		result.error = std::string("could not wait for process: ") + std::strerror(errno);
		return result;
	}

	if (WIFEXITED(processStatus))
		result.exitCode = WEXITSTATUS(processStatus);
	else if (WIFSIGNALED(processStatus))
		result.exitCode = 128 + WTERMSIG(processStatus);
	return result;
}
//---------------------------------------------------------------------------------------------------------------------------------//

}
