#include <caudio/runtime/process.h>

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <windows.h>
    #include <stdio.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <errno.h>
    #include <signal.h>
    #include <spawn.h>
#endif

struct ca_process {
#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE handle;
    DWORD  id;
#else
    pid_t pid;
#endif
};

ca_status ca_process_spawn(ca_process_t *out, char *const argv[],
                           char *const env[], ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr || argv == nullptr || argv[0] == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out or argv is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_process_t p = (ca_process_t)calloc(1, sizeof(struct ca_process));
    if (p == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate process");
        }
        return CA_OUT_OF_MEMORY;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    (void)env;

    /* Build a single command line from argv. */
    size_t cmd_len = 0;
    for (int i = 0; argv[i] != nullptr; ++i) {
        cmd_len += strlen(argv[i]) + 1; /* arg + space/separator */
    }

    char *cmdline = (char *)malloc(cmd_len + 1);
    if (cmdline == nullptr) {
        free(p);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate cmdline");
        }
        return CA_OUT_OF_MEMORY;
    }

    cmdline[0] = '\0';
    for (int i = 0; argv[i] != nullptr; ++i) {
        if (i > 0) {
            strcat(cmdline, " ");
        }
        strcat(cmdline, argv[i]);
    }

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    /* Convert the full command line to wide chars. CreateProcessW requires
       lpCommandLine to be a mutable buffer, so we pass cmdline directly. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, nullptr, 0);
    if (wlen <= 0) {
        free(cmdline);
        free(p);
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 cmdline");
        }
        return CA_INVALID_ARGUMENT;
    }

    LPWSTR wcmd = (LPWSTR)malloc((size_t)wlen * sizeof(WCHAR));
    if (wcmd == nullptr) {
        free(cmdline);
        free(p);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate cmdline");
        }
        return CA_OUT_OF_MEMORY;
    }

    MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
    free(cmdline);

    BOOL ok = CreateProcessW(nullptr, wcmd, nullptr, nullptr, FALSE,
                             0, nullptr, nullptr, &si, &pi);
    free(wcmd);

    if (!ok) {
        free(p);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "CreateProcess failed");
        }
        return CA_IO_ERROR;
    }

    p->handle = pi.hProcess;
    p->id = pi.dwProcessId;
    CloseHandle(pi.hThread);
#else
    int rc = posix_spawnp(&p->pid, argv[0], nullptr, nullptr, argv, env);
    if (rc != 0) {
        free(p);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "posix_spawnp failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    *out = p;
    return CA_OK;
}

ca_status ca_process_wait(ca_process_t process,
                          ca_process_status *status,
                          ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (process == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "process is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (WaitForSingleObject(process->handle, INFINITE) != WAIT_OBJECT_0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "WaitForSingleObject failed");
        }
        return CA_IO_ERROR;
    }

    DWORD code = 0;
    if (!GetExitCodeProcess(process->handle, &code)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "GetExitCodeProcess failed");
        }
        return CA_IO_ERROR;
    }

    CloseHandle(process->handle);

    if (status != nullptr) {
        status->exited = 1;
        status->code = (int)code;
    }
#else
    int wstatus = 0;
    pid_t rc = waitpid(process->pid, &wstatus, 0);
    if (rc < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "waitpid failed: %d", errno);
        }
        return CA_IO_ERROR;
    }

    if (status != nullptr) {
        if (WIFEXITED(wstatus)) {
            status->exited = 1;
            status->code = WEXITSTATUS(wstatus);
        } else if (WIFSIGNALED(wstatus)) {
            status->exited = 0;
            status->code = WTERMSIG(wstatus);
        } else {
            status->exited = 0;
            status->code = -1;
        }
    }
#endif

    free(process);
    return CA_OK;
}

ca_status ca_process_kill(ca_process_t process, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (process == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "process is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (!TerminateProcess(process->handle, 1)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "TerminateProcess failed");
        }
        return CA_IO_ERROR;
    }
#else
    if (kill(process->pid, SIGKILL) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "kill failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_process_release(ca_process_t process, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (process == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "process is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    CloseHandle(process->handle);
#else
    /* On POSIX, detaching means the child will be reparented to init. */
#endif

    free(process);
    return CA_OK;
}

int ca_process_self(void) {
#if defined(_WIN32) || defined(__CYGWIN__)
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}
