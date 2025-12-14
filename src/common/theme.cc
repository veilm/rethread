#include "common/theme.h"

namespace rethread {
namespace {
uint32_t g_background_color = kDefaultBackgroundColor;
}  // namespace

void SetDefaultBackgroundColor(uint32_t color) {
  g_background_color = color;
}

uint32_t GetDefaultBackgroundColor() {
  return g_background_color;
}

}  // namespace rethread
