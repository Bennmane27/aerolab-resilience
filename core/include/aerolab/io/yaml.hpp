// AEROLAB RESILIENCE - YAML subset reader.
//
// Requirements: DATA-002 (versioned scenario schema), SYS-010 (an invalid
// configuration is refused before the run starts).
//
// DEVIATION DEV-006 (docs/deviations.md): scenario files use the YAML syntax of
// section 7.1 of the cahier des charges, but only the subset below is accepted.
// A full YAML implementation would be a third party dependency an order of
// magnitude larger than this project's core, for a feature set (anchors,
// aliases, multi-document streams, tags, block scalars) that no scenario needs.
// Anything outside the subset is a hard parse error with a line number, never a
// silent misinterpretation.
//
// Supported
//   * block mappings          key: value  /  key:  + indented block
//   * block sequences         - item      /  - key: value
//   * flow collections        [a, b, c]   /  {k: v, k2: v2}
//   * scalars                 numbers, true/false, null, quoted and plain
//   * comments                # to end of line, outside quotes
//   * an optional leading ---
// Not supported (and rejected)
//   anchors &a, aliases *a, tags !!x, block scalars | and >, multiple documents
//
// The result is a Json tree, so scenario loading has exactly one code path
// whether the file was written as YAML or as JSON.
#pragma once

#include <string>

#include "aerolab/io/json.hpp"

namespace aerolab {

bool parseYaml(const std::string& text, Json& out, std::string& error);
bool parseYamlFile(const std::string& path, Json& out, std::string& error);

// Loads either .yaml/.yml or .json based on the file extension.
bool loadStructuredFile(const std::string& path, Json& out, std::string& error);

}  // namespace aerolab
