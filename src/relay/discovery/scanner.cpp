#include "relay/discovery/scanner.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace oak::relay::discovery {
namespace {

namespace fs = std::filesystem;

/// Reads at most [count] leading bytes of [path]. Returns how many were read.
std::size_t readHeader(const fs::path& path, std::uint8_t* out, std::size_t count) {
  // The path is passed as a path rather than as a narrow string so that the
  // platform's own wide-character API is used where it has one. A user's
  // directory names are not ours to mangle.
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return 0;

  stream.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(count));
  const std::streamsize got = stream.gcount();
  return got > 0 ? static_cast<std::size_t>(got) : 0;
}

void walk(const fs::path& directory,
          std::size_t depth,
          const ScanLimits& limits,
          ScanResult& result) {
  if (depth > limits.maxDepth) {
    result.truncated = true;
    return;
  }
  if (result.filesExamined >= limits.maxFiles) {
    result.truncated = true;
    return;
  }

  std::error_code iterationError;
  std::vector<fs::path> entries;
  for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied,
                                 iterationError),
       end;
       !iterationError && it != end; it.increment(iterationError)) {
    entries.push_back(it->path());
  }
  if (iterationError) return;  // A directory we cannot read is simply not read.

  std::sort(entries.begin(), entries.end());

  for (const fs::path& entry : entries) {
    if (result.filesExamined >= limits.maxFiles) {
      result.truncated = true;
      return;
    }

    std::error_code statusError;
    const fs::file_status status = fs::symlink_status(entry, statusError);
    if (statusError) continue;

    if (fs::is_symlink(status)) continue;

    if (fs::is_directory(status)) {
      walk(entry, depth + 1, limits, result);
      continue;
    }
    if (!fs::is_regular_file(status)) continue;

    ++result.filesExamined;

    std::uint8_t header[kModuleHeaderBytes] = {};
    const std::size_t got = readHeader(entry, header, sizeof(header));
    const ModuleFormat format = identifyModuleFormat(header, got);
    if (!isLoadableModuleFormat(format)) continue;

    Candidate candidate;
    candidate.id = "cand-" + std::to_string(result.candidates.size() + 1);
    candidate.path = entry.u8string();
    candidate.format = format;
    result.candidates.push_back(std::move(candidate));
  }
}

}  // namespace

ScanResult scanForCandidates(const std::vector<std::string>& roots, const ScanLimits& limits) {
  ScanResult result;

  for (const std::string& root : roots) {
    if (result.filesExamined >= limits.maxFiles) {
      result.truncated = true;
      break;
    }

    std::error_code error;
    const fs::file_status status = fs::symlink_status(fs::path(root), error);
    if (error || !fs::is_directory(status)) continue;

    walk(fs::path(root), 0, limits, result);
  }

  return result;
}

}  // namespace oak::relay::discovery
