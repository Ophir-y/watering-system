// Single admin password + bearer tokens for the REST API.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

class ConfigStore;

class Auth {
 public:
  void begin(ConfigStore& store);
  bool setupRequired();
  // Returns a new token, or "" when the password is wrong or logins are locked out.
  std::string login(const std::string& password);
  std::string setup(const std::string& password);
  bool check(const std::string& token);
  void logout(const std::string& token);
  bool changePassword(const std::string& current, const std::string& next, std::string& token);
  uint32_t lockedForMs() const;

  static constexpr size_t kMinPasswordLength = 8;

 private:
  std::string issue();

  ConfigStore* store_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  std::vector<std::string> hashes_;
  int failures_ = 0;
  uint32_t lockedUntilMs_ = 0;
};
