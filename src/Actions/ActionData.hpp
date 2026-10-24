#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Actions {

class ActionData {
public:
  static std::optional<ActionData> Parse(std::string_view source, std::string &error);
  static bool ValidateNoParameters(std::string_view source, std::string &error);
  // For actions whose payload is exactly one positive integer property named `key` (the ids from the query actions).
  static bool ParsePositiveId(std::string_view source, std::string_view key, int &id, std::string &error);

  ActionData(ActionData &&other) noexcept;
  ActionData &operator=(ActionData &&other) noexcept;
  ~ActionData();

  bool IsObject() const;
  size_t Size() const;
  bool HasProperty(std::string_view key) const;
  // Why an integer could not be read, so a message can say "not an integer" or "too large" instead of one fits-all.
  enum class IntegerResult { Ok, Missing, NotAnInteger, OutOfRange };
  IntegerResult ReadInteger(std::string_view key, int &value) const;
  bool GetInteger(std::string_view key, int &value) const { return ReadInteger(key, value) == IntegerResult::Ok; }
  // An integer of at least 1, or false with an error naming `key`.
  bool GetPositiveInteger(std::string_view key, int &value, std::string &error) const;
  bool GetString(std::string_view key, std::string &value) const;

private:
  explicit ActionData(void *root);
  // The first member called `key` (duplicate keys: the first wins), or null.
  const void *FindElement(std::string_view key) const;
  void *m_root = nullptr;
};

} // namespace Actions
