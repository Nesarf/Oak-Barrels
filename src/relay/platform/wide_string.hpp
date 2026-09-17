// Converting between the UTF-8 the relay speaks and the UTF-16 that some
// platform APIs require.
//
// The conversion is explicit rather than left to a default, because the
// alternative on this platform is that a narrow API interprets the bytes in the
// machine's code page -- and a user's directory names are not ours to mangle.
#pragma once

#ifdef _WIN32

#include <string>
#include <windows.h>

namespace oak::relay::platform {

/// Converts UTF-8 to UTF-16 for a platform API that wants it.
///
/// Returns an empty string when the input is empty or cannot be represented.
/// Callers treat empty as a refusal, never as a silently different name: a
/// wrong path that loads successfully is worse than one that fails.
inline std::wstring toWide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();

  const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
  if (length <= 0) return std::wstring();

  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                        static_cast<int>(utf8.size()), wide.data(), length);
  return wide;
}

}  // namespace oak::relay::platform

#endif  // _WIN32
