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

  ActionData(ActionData &&other) noexcept;
  ActionData &operator=(ActionData &&other) noexcept;
  ~ActionData();

  bool IsObject() const;
  size_t Size() const;
  bool GetInteger(std::string_view key, int &value) const;

private:
  explicit ActionData(void *root);
  void *m_root = nullptr;
};

} // namespace Actions
