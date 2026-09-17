#include "harness.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "relay/discovery/scanner.hpp"

namespace {

namespace fs = std::filesystem;

using oak::relay::discovery::ModuleFormat;
using oak::relay::discovery::scanForCandidates;
using oak::relay::discovery::ScanLimits;
using oak::relay::discovery::ScanResult;

/// A throwaway directory tree that removes itself.
class TempTree {
 public:
  explicit TempTree(const std::string& tag) {
    static int counter = 0;
    std::error_code error;
    path_ = fs::temp_directory_path(error) /
            ("oak-barrels-scan-" + tag + "-" + std::to_string(counter++));
    fs::remove_all(path_, error);
    fs::create_directories(path_, error);
  }

  ~TempTree() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  TempTree(const TempTree&) = delete;
  TempTree& operator=(const TempTree&) = delete;

  const fs::path& path() const { return path_; }

  void writeElf(const std::string& relative) const {
    writeBytes(relative, {0x7f, 'E', 'L', 'F', 2, 1, 1, 0});
  }

  void writePe(const std::string& relative) const {
    std::vector<std::uint8_t> header(0x48, 0);
    header[0] = 'M';
    header[1] = 'Z';
    header[0x3c] = 0x40;
    header[0x40] = 'P';
    header[0x41] = 'E';
    writeBytes(relative, header);
  }

  void writeText(const std::string& relative) const {
    writeBytes(relative, {'n', 'o', 't', 'a', 'm', 'o', 'd', 'u', 'l', 'e'});
  }

  void makeDirectory(const std::string& relative) const {
    std::error_code error;
    fs::create_directories(path_ / relative, error);
  }

  void writeBytes(const std::string& relative, const std::vector<std::uint8_t>& data) const {
    const fs::path target = path_ / relative;
    std::error_code error;
    fs::create_directories(target.parent_path(), error);

    std::ofstream stream(target, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
  }

 private:
  fs::path path_;
};

}  // namespace

OAK_TEST(scanner_finds_nothing_in_an_empty_tree) {
  const TempTree tree("empty");

  const ScanResult result = scanForCandidates({tree.path().u8string()});
  OAK_CHECK_EQ(result.candidates.size(), 0u);
  OAK_CHECK_EQ(result.filesExamined, 0u);
  OAK_CHECK(!result.truncated);
}

OAK_TEST(scanner_finds_modules_and_ignores_everything_else) {
  const TempTree tree("mixed");
  tree.writeElf("a-module");
  tree.writePe("b-module");
  tree.writeText("notes.txt");

  const ScanResult result = scanForCandidates({tree.path().u8string()});
  OAK_CHECK_EQ(result.filesExamined, 3u);
  OAK_CHECK_EQ(result.candidates.size(), 2u);

  // Sorted by path, so "a-module" comes first.
  OAK_CHECK(result.candidates[0].format == ModuleFormat::Elf);
  OAK_CHECK(result.candidates[1].format == ModuleFormat::PortableExecutable);
}

OAK_TEST(scanner_recurses_but_stops_at_the_depth_limit) {
  const TempTree tree("depth");
  tree.writeElf("top");
  tree.writeElf("one/second");
  tree.writeElf("one/one/third");

  const ScanResult shallow = scanForCandidates({tree.path().u8string()}, ScanLimits{4096, 1});
  OAK_CHECK_EQ(shallow.candidates.size(), 2u);  // top, and one/second
  OAK_CHECK(shallow.truncated);

  const ScanResult deep = scanForCandidates({tree.path().u8string()}, ScanLimits{4096, 4});
  OAK_CHECK_EQ(deep.candidates.size(), 3u);
  OAK_CHECK(!deep.truncated);
}

OAK_TEST(scanner_skips_roots_that_are_not_directories) {
  const TempTree tree("badroot");
  tree.writeElf("a-module");

  const std::string missing = (tree.path() / "does-not-exist").u8string();
  OAK_CHECK_EQ(scanForCandidates({missing}).candidates.size(), 0u);

  // A file nominated as a root is not an error either; it is simply not walked.
  const std::string aFile = (tree.path() / "a-module").u8string();
  OAK_CHECK_EQ(scanForCandidates({aFile}).candidates.size(), 0u);
}

OAK_TEST(scanner_is_deterministic) {
  const TempTree tree("order");
  tree.writeElf("m3");
  tree.writeElf("m1");
  tree.writeElf("m2");

  const ScanResult first = scanForCandidates({tree.path().u8string()});
  const ScanResult second = scanForCandidates({tree.path().u8string()});

  OAK_CHECK_EQ(first.candidates.size(), 3u);
  OAK_CHECK_EQ(second.candidates.size(), 3u);
  for (std::size_t i = 0; i < first.candidates.size(); ++i) {
    OAK_CHECK_EQ(first.candidates[i].path, second.candidates[i].path);
  }
  // Sorted, not left in whatever order the filesystem returned.
  OAK_CHECK(first.candidates[0].path.find("m1") != std::string::npos);
}

OAK_TEST(scanner_reports_truncation_when_the_file_cap_is_reached) {
  const TempTree tree("cap");
  for (int i = 0; i < 5; ++i) {
    tree.writeText("f" + std::to_string(i));
  }

  const ScanResult result = scanForCandidates({tree.path().u8string()}, ScanLimits{2, 4});
  OAK_CHECK_EQ(result.filesExamined, 2u);
  OAK_CHECK(result.truncated);
}

OAK_TEST(scanner_ids_are_opaque_and_do_not_leak_the_path) {
  const TempTree tree("ids");
  tree.writeElf("a-rather-revealing-name");

  const ScanResult result = scanForCandidates({tree.path().u8string()});
  OAK_CHECK_EQ(result.candidates.size(), 1u);
  // The id is the part that may travel. It says nothing about where the file
  // came from, which is the whole reason it exists.
  OAK_CHECK_EQ(result.candidates[0].id, std::string("cand-1"));
  OAK_CHECK(result.candidates[0].id.find("revealing") == std::string::npos);
}

OAK_TEST(scanner_does_not_follow_symlinks) {
  const TempTree tree("links");
  tree.writeElf("real-module");

  // A tree outside the nominated root, reachable only by following a link.
  const fs::path outside =
      tree.path().parent_path() / (tree.path().filename().u8string() + "-outside");
  std::error_code error;
  fs::remove_all(outside, error);
  fs::create_directories(outside, error);
  {
    std::ofstream stream(outside / "outside-module", std::ios::binary);
    const std::vector<std::uint8_t> elf = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
    stream.write(reinterpret_cast<const char*>(elf.data()),
                 static_cast<std::streamsize>(elf.size()));
  }

  fs::create_directory_symlink(outside, tree.path() / "link", error);
  if (error) {
    // Creating a symlink needs a privilege the runner may not have. The
    // behaviour is still covered wherever symlinks can be made.
    fs::remove_all(outside, error);
    return;
  }

  const ScanResult result = scanForCandidates({tree.path().u8string()});
  // Only the real module. Following the link would turn a nominated directory
  // into permission to walk wherever the link points.
  OAK_CHECK_EQ(result.candidates.size(), 1u);
  OAK_CHECK(result.candidates[0].path.find("real-module") != std::string::npos);

  fs::remove_all(outside, error);
}
