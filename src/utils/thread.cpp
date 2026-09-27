#include <caudio/utils/thread.hpp>

namespace caudio::utils::detail {

#if defined(_WIN32) || defined(_WIN64)
/// UTF-8 code page for MultiByteToWideChar (matches CP_UTF8 without windows.h).
inline constexpr unsigned kUtf8CodePage = 65001;
Expected<void> setNativeHandleName(void* nativeHandle, std::string_view name) noexcept {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        auto pSetDesc =
            reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(k32, "SetThreadDescription"));
#pragma GCC diagnostic pop
        if (pSetDesc) {
            int wlen = MultiByteToWideChar(kUtf8CodePage, 0, name.data(), static_cast<int>(name.size()),
                                           nullptr, 0);
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
constexpr std::string_view truncate15(std::string_view s) noexcept {
    return s.substr(0, 15);
}

int setPthreadName(pthread_t th, std::string_view name) noexcept {
    std::string_view t = truncate15(name);
    char buf[16]{};
    if (!t.empty())
        std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
    return pthread_setname_np(th, buf);
}
#endif

} // namespace caudio::utils::detail
