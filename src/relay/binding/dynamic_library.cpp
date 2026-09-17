#include "relay/binding/dynamic_library.hpp"

#include <utility>

#ifdef _WIN32
#include <windows.h>

#include "relay/platform/wide_string.hpp"
#else
#include <dlfcn.h>
#endif

namespace oak::relay::binding {

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    release();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

DynamicLibrary::~DynamicLibrary() { release(); }

void DynamicLibrary::release() {
  if (handle_ == nullptr) return;

#ifdef _WIN32
  ::FreeLibrary(static_cast<HMODULE>(handle_));
#else
  ::dlclose(handle_);
#endif

  handle_ = nullptr;
}

DynamicLibrary DynamicLibrary::load(const std::string& path, protocol::Reason& reason) {
  DynamicLibrary library;

  if (path.empty()) {
    reason = protocol::Reason::BadArgument;
    return library;
  }

#ifdef _WIN32
  const std::wstring wide = oak::relay::platform::toWide(path);
  if (wide.empty()) {
    reason = protocol::Reason::BadArgument;
    return library;
  }

  // Loaded with altered search paths off: a module that pulls its dependencies
  // from whatever happens to be in the working directory is a module that can
  // be hijacked by one.
  HMODULE handle = ::LoadLibraryExW(wide.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (handle == nullptr) {
    handle = ::LoadLibraryW(wide.c_str());
  }
  if (handle == nullptr) {
    reason = protocol::Reason::BindFailed;
    return library;
  }
  library.handle_ = static_cast<void*>(handle);
#else
  // RTLD_LOCAL, so that a module's symbols do not leak into the global
  // namespace and collide with the next candidate we try.
  void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    reason = protocol::Reason::BindFailed;
    return library;
  }
  library.handle_ = handle;
#endif

  reason = protocol::Reason::Ok;
  return library;
}

void* DynamicLibrary::resolve(const std::string& symbol) const {
  if (handle_ == nullptr || symbol.empty()) return nullptr;

#ifdef _WIN32
  return reinterpret_cast<void*>(
      ::GetProcAddress(static_cast<HMODULE>(handle_), symbol.c_str()));
#else
  // dlsym returns void*, and the cast through a function pointer would be the
  // usual dance. We hand the raw address back and let the caller decide what
  // shape it is, which is the only place that decision belongs.
  return ::dlsym(handle_, symbol.c_str());
#endif
}

}  // namespace oak::relay::binding
