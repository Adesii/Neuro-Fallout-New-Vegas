#pragma once
// Minimal self-registering test harness so the host tests need no third-party download.
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace Test {

struct Case {
  const char *name;
  std::function<void()> run;
};

struct RequireFailure : std::exception {};

inline std::vector<Case> &Cases() {
  static std::vector<Case> cases;
  return cases;
}

inline int &Failures() {
  static int failures = 0;
  return failures;
}

struct Registrar {
  Registrar(const char *name, std::function<void()> run) { Cases().push_back({name, std::move(run)}); }
};

inline void Fail(const char *file, int line, const std::string &message) {
  ++Failures();
  std::cerr << "    FAIL " << file << ":" << line << ": " << message << "\n";
}

template <class A, class B>
void CheckEqual(const char *file, int line, const char *aText, const char *bText, const A &a, const B &b) {
  if (a == b)
    return;
  std::ostringstream message;
  message << aText << " == " << bText << " (got [" << a << "] vs [" << b << "])";
  Fail(file, line, message.str());
}

inline int RunAll(int argc, char **argv) {
  const std::string filter = argc > 1 ? argv[1] : "";
  int ran = 0;
  int failedCases = 0;
  for (const auto &testCase : Cases()) {
    if (!filter.empty() && std::string(testCase.name).find(filter) == std::string::npos)
      continue;
    const int before = Failures();
    try {
      testCase.run();
    } catch (const RequireFailure &) {
    } catch (const std::exception &error) {
      Fail(__FILE__, __LINE__, std::string("unexpected exception: ") + error.what());
    }
    ++ran;
    if (Failures() != before) {
      ++failedCases;
      std::cerr << "  FAILED " << testCase.name << "\n";
    }
  }
  std::cout << ran << " test(s) run, " << failedCases << " failed\n";
  return failedCases == 0 ? 0 : 1;
}

} // namespace Test

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)
#define TEST(name)                                                                                                     \
  static void TEST_CONCAT(test_, __LINE__)();                                                                          \
  static ::Test::Registrar TEST_CONCAT(registrar_, __LINE__)(name, TEST_CONCAT(test_, __LINE__));                      \
  static void TEST_CONCAT(test_, __LINE__)()

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition))                                                                                                  \
      ::Test::Fail(__FILE__, __LINE__, #condition);                                                                    \
  } while (0)
#define CHECK_EQ(a, b) ::Test::CheckEqual(__FILE__, __LINE__, #a, #b, (a), (b))
// Abort the current test: later steps depend on this one.
#define REQUIRE(condition)                                                                                             \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      ::Test::Fail(__FILE__, __LINE__, #condition);                                                                    \
      throw ::Test::RequireFailure();                                                                                  \
    }                                                                                                                  \
  } while (0)
