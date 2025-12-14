#ifndef RETHREAD_COMMON_THEME_H_
#define RETHREAD_COMMON_THEME_H_

#include <cstdint>

namespace rethread {

constexpr uint32_t kDefaultBackgroundColor = 0xFF333333;

void SetDefaultBackgroundColor(uint32_t color);
uint32_t GetDefaultBackgroundColor();

}  // namespace rethread

#endif  // RETHREAD_COMMON_THEME_H_
