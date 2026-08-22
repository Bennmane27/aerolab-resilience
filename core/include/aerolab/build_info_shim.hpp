// AEROLAB RESILIENCE - build provenance accessors.
//
// SYS-008 / DATA-005: every manifest carries the commit, version, build type
// and compiler that produced it. cmake/build_info.hpp.in is configured into the
// build tree; this shim keeps the core compilable outside CMake (a bare g++
// invocation, an IDE index pass) without pretending to know a commit it cannot
// see. "unknown" in a manifest is a fact; a hard coded fake would be a lie.
#pragma once

#if defined(__has_include)
#if __has_include("aerolab/build_info.hpp")
#include "aerolab/build_info.hpp"
#define AEROLAB_HAS_BUILD_INFO 1
#endif
#endif

namespace aerolab {

inline const char* buildVersion() {
#ifdef AEROLAB_HAS_BUILD_INFO
  return kVersion;
#else
  return "1.0.0-nocmake";
#endif
}

inline const char* buildGitCommit() {
#ifdef AEROLAB_HAS_BUILD_INFO
  return kGitCommit;
#else
  return "unknown";
#endif
}

inline const char* buildType() {
#ifdef AEROLAB_HAS_BUILD_INFO
  return kBuildType;
#else
  return "unknown";
#endif
}

inline const char* buildCompiler() {
#ifdef AEROLAB_HAS_BUILD_INFO
  return kCompilerId;
#else
#if defined(__EMSCRIPTEN__)
  return "Emscripten";
#elif defined(__clang__)
  return "Clang";
#elif defined(__GNUC__)
  return "GNU";
#elif defined(_MSC_VER)
  return "MSVC";
#else
  return "unknown";
#endif
#endif
}

inline const char* buildCompilerVersion() {
#ifdef AEROLAB_HAS_BUILD_INFO
  return kCompilerVersion;
#else
  return "unknown";
#endif
}

}  // namespace aerolab
