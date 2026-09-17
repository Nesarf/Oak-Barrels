// Finding module files under roots the host nominated.
//
// The relay invents no search locations of its own. A relay that decided where
// to look would be surveying the user's disk on its own initiative, and
// "surveys the machine unprompted" is the behaviour this entire project exists
// to avoid. The host says where to look; the relay says what it found, in terms
// that identify nothing.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "relay/discovery/module_format.hpp"

namespace oak::relay::discovery {

/// A module file that might be worth probing.
///
/// `path` is a discovered fact. Per axiom A4 it stays inside this process: not
/// logged, not written to disk, not sent over the pipe. What crosses the pipe
/// is a compatibility class, or a count, or nothing at all.
struct Candidate {
  std::string id;  ///< Opaque, and stable only for the lifetime of one session.
  std::string path;
  ModuleFormat format = ModuleFormat::Unknown;
};

/// Bounds on a single scan.
///
/// Every one of these exists so that a hostile or careless root cannot turn
/// discovery into an unbounded disk walk.
struct ScanLimits {
  std::size_t maxFiles = 4096;
  std::size_t maxDepth = 4;
};

struct ScanResult {
  std::vector<Candidate> candidates;

  /// How many files were opened, whether or not they turned out to be modules.
  std::size_t filesExamined = 0;

  /// True when a limit stopped the walk early, so the result is partial.
  bool truncated = false;
};

/// Enumerates candidate modules beneath [roots].
///
/// Directories that do not exist, cannot be read, or are not directories are
/// skipped silently: a host may nominate a list that is right on one machine
/// and partly wrong on another, and that is not an error worth reporting.
///
/// Symlinks are not followed. A link is a claim about where something else
/// lives, and following one turns a nominated directory into permission to walk
/// wherever it points.
///
/// Results are ordered deterministically, so that a reported count is
/// reproducible and a test cannot pass by luck.
ScanResult scanForCandidates(const std::vector<std::string>& roots,
                             const ScanLimits& limits = {});

}  // namespace oak::relay::discovery
