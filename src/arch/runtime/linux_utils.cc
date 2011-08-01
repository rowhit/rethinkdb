#include "arch/runtime/linux_utils.hpp"

#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>

#include "logger.hpp"

int _gettid() {
    return syscall(SYS_gettid);
}

fd_t scoped_fd_t::reset(fd_t f2) {
    if (fd != INVALID_FD) {
        int res = close(fd);
        if (res != 0) logERR("Error in close(): %s\n", strerror(errno));
    }
    fd = f2;
    return f2;
}

int get_cpu_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
