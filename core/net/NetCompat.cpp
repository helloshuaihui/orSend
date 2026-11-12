#include "NetCompat.h"

#include "Logger.h"

namespace orsend {

#ifdef _WIN32
static int g_wsaRefCount = 0;
static bool g_wsaReady = false;
#endif

bool NetRuntime::Init() {
#ifdef _WIN32
    if (g_wsaReady) {
        ++g_wsaRefCount;
        return true;
    }
    WSADATA data;
    int rc = WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) {
        LOG_ERROR("net", "WSAStartup 失败: " << rc);
        return false;
    }
    g_wsaReady = true;
    g_wsaRefCount = 1;
    return true;
#else
    return true;
#endif
}

void NetRuntime::Shutdown() {
#ifdef _WIN32
    if (!g_wsaReady) return;
    if (--g_wsaRefCount > 0) return;
    WSACleanup();
    g_wsaReady = false;
    g_wsaRefCount = 0;
#endif
}

}  // namespace orsend
