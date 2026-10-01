#include <caudio/utils/error.hpp>
#include <caudio/utils/thread.hpp>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace caudio::utils::detail {

#if defined(_WIN32) || defined(_WIN64)
/// UTF-8 code page for MultiByteToWideChar (matches CP_UTF8 without windows.h).
inline constexpr unsigned kUtf8CodePage = 65001;
Expected<void> setNativeHandleName(void* nativeHandle, std::string_view name) noexcept {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto pSetDesc =
            reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(k32, "SetThreadDescription"));
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
        if (pSetDesc) {
            int wlen = MultiByteToWideChar(kUtf8CodePage, 0, name.data(),
                                           static_cast<int>(name.size()), nullptr, 0);
            if (wlen > 0) {
                std::wstring wbuf(static_cast<std::size_t>(wlen), L'\0');
                MultiByteToWideChar(kUtf8CodePage, 0, name.data(), static_cast<int>(name.size()),
                                    wbuf.data(), wlen);
                HRESULT hr = pSetDesc(reinterpret_cast<HANDLE>(nativeHandle), wbuf.c_str());
                (void)hr;
                return {};
            }
        }
    }
    return {};
}

Expected<void> setCurrentThreadNameImpl(std::string_view name) noexcept {
    return setNativeHandleName(GetCurrentThread(), name);
}
#else
int setPthreadName(pthread_t th, std::string_view name) noexcept {
    std::string_view t = truncate15(name);
    char buf[16]{};
    if (!t.empty())
        std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
#ifdef __APPLE__
    // macOS can only name the calling thread (no thread-targeted variant).
    if (!pthread_equal(th, pthread_self()))
        return 0;
    return pthread_setname_np(buf);
#else
    return pthread_setname_np(th, buf);
#endif
}
#endif

} // namespace caudio::utils::detail
