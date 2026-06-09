#include "process_runner.h"

#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>

#include "embedded/embedded_assets_runtime.h"

namespace {

static const ProcessRunner::Hooks *g_hooks = nullptr;

struct Fd {
    int fd = -1;
    Fd() = default;
    explicit Fd(int f) : fd(f) {}
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd &&o) noexcept : fd(o.fd) { o.fd = -1; }
    Fd &operator=(Fd &&o) noexcept
    {
        if (this == &o) {
            return *this;
        }
        if (fd >= 0) {
            ::close(fd);
        }
        fd = o.fd;
        o.fd = -1;
        return *this;
    }
    ~Fd()
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

bool set_nonblock(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void append_all(std::string &dst, const char *data, size_t n)
{
    dst.append(data, n);
}

static void maybe_forward(const std::function<void(const char *, size_t)> &cb, const char *data, size_t n)
{
    if (cb) {
        cb(data, n);
    }
}

static bool terminate_child_on_interrupt(pid_t pid)
{
    if (!EmbeddedAssetsRuntime::signalStopRequested()) {
        return false;
    }

    (void)::kill(pid, SIGTERM);

    for (int attempt = 0; attempt < 60; ++attempt) {
        int status = 0;
        const pid_t wp = ::waitpid(pid, &status, WNOHANG);
        if (wp == pid) {
            return true;
        }
        ::usleep(50'000);
    }

    (void)::kill(pid, SIGKILL);
    int status = 0;
    (void)::waitpid(pid, &status, 0);
    return true;
}

static ProcessRunner::Result run_impl(const std::string &program, const std::vector<std::string> &args,
                                     const std::string &stdinText,
                                     const std::function<void(const char *, size_t)> &onStdout,
                                     const std::function<void(const char *, size_t)> &onStderr,
                                     int timeout_ms)
{
    ProcessRunner::Result r;

    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int exec_pipe[2] = {-1, -1};

    if (::pipe(in_pipe) != 0) {
        return r;
    }
    if (::pipe(out_pipe) != 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        return r;
    }
    if (::pipe(err_pipe) != 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return r;
    }

    if (::pipe(exec_pipe) != 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return r;
    }

    Fd in_r(in_pipe[0]);
    Fd in_w(in_pipe[1]);
    Fd out_r(out_pipe[0]);
    Fd out_w(out_pipe[1]);
    Fd err_r(err_pipe[0]);
    Fd err_w(err_pipe[1]);
    Fd exec_r(exec_pipe[0]);
    Fd exec_w(exec_pipe[1]);

    {
        const int flags = ::fcntl(exec_w.fd, F_GETFD, 0);
        if (flags >= 0) {
            (void)::fcntl(exec_w.fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        return r;
    }

    if (pid == 0) {
        (void)::dup2(in_r.fd, STDIN_FILENO);
        (void)::dup2(out_w.fd, STDOUT_FILENO);
        (void)::dup2(err_w.fd, STDERR_FILENO);

        ::close(in_w.fd);
        ::close(out_r.fd);
        ::close(err_r.fd);
        ::close(exec_r.fd);

        std::vector<char *> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char *>(program.c_str()));
        for (const std::string &a : args) {
            argv.push_back(const_cast<char *>(a.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(program.c_str(), argv.data());

        const int e = errno;
        (void)::write(exec_w.fd, &e, sizeof(e));
        _exit(127);
    }

    ::close(in_r.fd);
    in_r.fd = -1;
    ::close(out_w.fd);
    out_w.fd = -1;
    ::close(err_w.fd);
    err_w.fd = -1;
    ::close(exec_w.fd);
    exec_w.fd = -1;

    (void)set_nonblock(exec_r.fd);

    {
        for (;;) {
            int exec_errno = 0;
            const ssize_t n = ::read(exec_r.fd, &exec_errno, sizeof(exec_errno));
            if (n > 0) {
                int status = 0;
                (void)::waitpid(pid, &status, 0);
                r.started = false;
                r.exitStatus = ProcessRunner::ExitStatus::FailedToStart;
                r.exitCode = 127;
                return r;
            }
            if (n == 0) {
                break;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }

            struct pollfd pfd;
            pfd.fd = exec_r.fd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            const int pr = ::poll(&pfd, 1, 10);
            if (pr <= 0) {
                break;
            }
        }
    }

    r.started = true;

    (void)set_nonblock(out_r.fd);
    (void)set_nonblock(err_r.fd);

    size_t stdinPos = 0;
    bool stdinClosed = false;
    bool outClosed = false;
    bool errClosed = false;

    auto start = std::chrono::steady_clock::now();

    while (true) {
        if (terminate_child_on_interrupt(pid)) {
            r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
            r.exitCode = SIGTERM;
            return r;
        }

        if (timeout_ms >= 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed > timeout_ms) {
                (void)::kill(pid, SIGKILL);
                int status = 0;
                (void)::waitpid(pid, &status, 0);
                r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
                r.exitCode = SIGKILL;
                return r;
            }
        }

        struct pollfd fds[3];
        int nfds = 0;
        if (!stdinClosed && stdinPos < stdinText.size()) {
            fds[nfds].fd = in_w.fd;
            fds[nfds].events = POLLOUT;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (!outClosed) {
            fds[nfds].fd = out_r.fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (!errClosed) {
            fds[nfds].fd = err_r.fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }

        if (nfds == 0) {
            int status = 0;
            const pid_t wp = ::waitpid(pid, &status, WNOHANG);
            if (wp == pid) {
                if (WIFEXITED(status)) {
                    r.exitStatus = ProcessRunner::ExitStatus::NormalExit;
                    r.exitCode = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
                    r.exitCode = WTERMSIG(status);
                } else {
                    r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
                    r.exitCode = 1;
                }
                return r;
            }

            ::usleep(1000);
            continue;
        }

        const int pr = ::poll(fds, static_cast<nfds_t>(nfds), 50);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
            r.exitCode = 1;
            return r;
        }

        int idx = 0;
        if (!stdinClosed && stdinPos < stdinText.size()) {
            if (fds[idx].revents & POLLOUT) {
                const size_t remaining = stdinText.size() - stdinPos;
                const size_t chunk = remaining > 4096 ? 4096 : remaining;
                const ssize_t n = ::write(in_w.fd, stdinText.data() + stdinPos, chunk);
                if (n > 0) {
                    stdinPos += static_cast<size_t>(n);
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    stdinClosed = true;
                    ::close(in_w.fd);
                    in_w.fd = -1;
                }
            }
            ++idx;
        }

        if (!outClosed) {
            if (fds[idx].revents & (POLLIN | POLLHUP)) {
                char buf[4096];
                const ssize_t n = ::read(out_r.fd, buf, sizeof(buf));
                if (n > 0) {
                    append_all(r.stdoutText, buf, static_cast<size_t>(n));
                    maybe_forward(onStdout, buf, static_cast<size_t>(n));
                } else {
                    outClosed = true;
                    ::close(out_r.fd);
                    out_r.fd = -1;
                }
            }
            ++idx;
        }

        if (!errClosed) {
            if (fds[idx].revents & (POLLIN | POLLHUP)) {
                char buf[4096];
                const ssize_t n = ::read(err_r.fd, buf, sizeof(buf));
                if (n > 0) {
                    append_all(r.stderrText, buf, static_cast<size_t>(n));
                    maybe_forward(onStderr, buf, static_cast<size_t>(n));
                } else {
                    errClosed = true;
                    ::close(err_r.fd);
                    err_r.fd = -1;
                }
            }
        }

        if (!stdinClosed && stdinPos >= stdinText.size()) {
            stdinClosed = true;
            ::close(in_w.fd);
            in_w.fd = -1;
        }

        int status = 0;
        const pid_t wp = ::waitpid(pid, &status, WNOHANG);
        if (wp == pid) {
            if (WIFEXITED(status)) {
                r.exitStatus = ProcessRunner::ExitStatus::NormalExit;
                r.exitCode = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
                r.exitCode = WTERMSIG(status);
            } else {
                r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
                r.exitCode = 1;
            }

            char buf[4096];
            while (!outClosed) {
                const ssize_t n = ::read(out_r.fd, buf, sizeof(buf));
                if (n > 0) {
                    append_all(r.stdoutText, buf, static_cast<size_t>(n));
                    maybe_forward(onStdout, buf, static_cast<size_t>(n));
                } else {
                    break;
                }
            }
            while (!errClosed) {
                const ssize_t n = ::read(err_r.fd, buf, sizeof(buf));
                if (n > 0) {
                    append_all(r.stderrText, buf, static_cast<size_t>(n));
                    maybe_forward(onStderr, buf, static_cast<size_t>(n));
                } else {
                    break;
                }
            }

            return r;
        }
    }

    r.exitStatus = ProcessRunner::ExitStatus::CrashExit;
    r.exitCode = 1;
    return r;
}

} // namespace

void ProcessRunner::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

ProcessRunner::Result ProcessRunner::run(const std::string &program, const std::vector<std::string> &args,
                                        const std::string &stdinText, int timeout_ms)
{
    if (g_hooks && g_hooks->run) {
        return g_hooks->run(program, args, stdinText, timeout_ms);
    }
    return run_impl(program, args, stdinText, std::function<void(const char *, size_t)>(),
                    std::function<void(const char *, size_t)>(), timeout_ms);
}

ProcessRunner::Result ProcessRunner::runStreaming(const std::string &program, const std::vector<std::string> &args,
                                                 const std::string &stdinText,
                                                 const std::function<void(const char *, size_t)> &onStdout,
                                                 const std::function<void(const char *, size_t)> &onStderr,
                                                 int timeout_ms)
{
    if (g_hooks && g_hooks->runStreaming) {
        return g_hooks->runStreaming(program, args, stdinText, onStdout, onStderr, timeout_ms);
    }
    return run_impl(program, args, stdinText, onStdout, onStderr, timeout_ms);
}

int ProcessRunner::execute(const std::string &program, const std::vector<std::string> &args, int timeout_ms)
{
    if (g_hooks && g_hooks->execute) {
        return g_hooks->execute(program, args, timeout_ms);
    }
    const Result r = run(program, args, std::string(), timeout_ms);
    if (!r.started || r.exitStatus == ExitStatus::FailedToStart) {
        return -2;
    }
    if (r.exitStatus == ExitStatus::NormalExit) {
        return r.exitCode;
    }
    return -1;
}
