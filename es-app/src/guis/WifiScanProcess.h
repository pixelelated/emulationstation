#pragma once

// A bounded, read-only subprocess for the Wi-Fi picker (#577). No shell,
// GUI, singleton or callback is owned here. Kept separate so a disposable
// process fixture can exercise the actual runner without linking ES.
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#if !WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace WifiScanProcess
{
struct Answer
{
	int code = -1; // spawn/read/signal/timeout/cleanup failure is never empty success
	std::vector<std::string> lines;
};

#if !WIN32
class Child
{
public:
	Child(pid_t child, int output) : pid(child), fd(output) {}
	Child(const Child&) = delete;
	Child& operator=(const Child&) = delete;
	~Child() { finish(); }

	bool finish()
	{
		if (finished)
			return cleaned;
		finished = true;
		close(fd);
		// Even a reaped leader may have descendants holding the pipe. The
		// spawn creates this group; never signal ES's own process group.
		cleaned = kill(-pid, SIGKILL) == 0 || errno == ESRCH;
		if (!cleaned)
			kill(pid, SIGKILL);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!reaped)
		{
			const pid_t waited = waitpid(pid, &status, WNOHANG);
			if (waited == pid)
				reaped = true;
			else if ((waited < 0 && errno != EINTR) || std::chrono::steady_clock::now() >= deadline)
				return cleaned = false;
			else
				poll(nullptr, 0, 10);
		}
		return cleaned;
	}

	pid_t pid;
	int fd;
	int status = 0;
	bool reaped = false;
private:
	bool finished = false;
	bool cleaned = false;
};
#endif

inline Answer read(const std::vector<std::string>& command, std::chrono::milliseconds timeout,
	const std::atomic<bool>* cancelled = nullptr)
{
	Answer answer;
#if !WIN32
	const auto stopped = [cancelled]
	{ return cancelled && cancelled->load(std::memory_order_acquire); };
	if (command.empty() || timeout.count() <= 0 || stopped())
		return answer;
	std::vector<char*> argv;
	for (const auto& part : command)
		argv.push_back(const_cast<char*>(part.c_str()));
	argv.push_back(nullptr);

	int output[2];
	if (pipe2(output, O_CLOEXEC) != 0)
		return answer;
	// File actions must not close stdout after dup2 when an inherited
	// standard descriptor was absent and pipe2 reused its number.
	for (int& descriptor : output)
		if (descriptor <= STDERR_FILENO)
		{
			const int moved = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
			if (moved == -1)
			{
				close(output[0]); close(output[1]);
				return answer;
			}
			close(descriptor);
			descriptor = moved;
		}
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attributes;
	if (posix_spawn_file_actions_init(&actions) != 0)
	{
		close(output[0]); close(output[1]);
		return answer;
	}
	if (posix_spawnattr_init(&attributes) != 0)
	{
		posix_spawn_file_actions_destroy(&actions);
		close(output[0]); close(output[1]);
		return answer;
	}
	pid_t pid = -1;
	const bool configured =
		posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO) == 0 &&
		posix_spawn_file_actions_addclose(&actions, output[0]) == 0 &&
		posix_spawn_file_actions_addclose(&actions, output[1]) == 0 &&
		posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0 &&
		posix_spawnattr_setpgroup(&attributes, 0) == 0;
	const int spawned = configured && !stopped() ? posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ) : -1;
	posix_spawnattr_destroy(&attributes);
	posix_spawn_file_actions_destroy(&actions);
	close(output[1]);
	if (spawned != 0)
	{
		close(output[0]);
		return answer;
	}

	Child child(pid, output[0]);
	const int flags = fcntl(child.fd, F_GETFL);
	if (flags == -1 || fcntl(child.fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return answer;
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	std::string outputText;
	bool eof = false;
	while (true)
	{
		if (stopped())
			return answer;
		char buffer[4096];
		while (!eof)
		{
			if (stopped())
				return answer;
			const ssize_t count = ::read(child.fd, buffer, sizeof(buffer));
			if (count > 0)
			{
				// A corrupt/unbounded command must not grow the UI's heap.
				if (outputText.size() + count > 256 * 1024 || std::chrono::steady_clock::now() >= deadline)
					return answer;
				outputText.append(buffer, count);
			}
			else if (count == 0)
				eof = true;
			else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				break;
			else
				return answer;
		}
		if (!child.reaped)
		{
			const pid_t waited = waitpid(child.pid, &child.status, WNOHANG);
			if (waited == child.pid)
				child.reaped = true;
			else if (waited < 0 && errno != EINTR)
				return answer;
		}
		if (eof && child.reaped)
			break;
		if (std::chrono::steady_clock::now() >= deadline)
			return answer;
		pollfd descriptor{eof ? -1 : child.fd, POLLIN, 0};
		if (poll(&descriptor, 1, 25) < 0 && errno != EINTR)
			return answer;
	}
	if (stopped() || !child.finish() || !WIFEXITED(child.status))
		return answer;
	answer.code = WEXITSTATUS(child.status);
	if (answer.code == 0)
	{
		size_t start = 0;
		while (start < outputText.size())
		{
			const size_t end = outputText.find('\n', start);
			const std::string line = outputText.substr(start, end == std::string::npos ? end : end - start);
			if (!line.empty())
				answer.lines.push_back(line);
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
	}
#endif
	return answer;
}
}
