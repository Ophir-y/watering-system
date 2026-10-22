#include "WebAssets.h"

#include <string.h>

#if __has_include("../../generated/web_assets.h")
#include "../../generated/web_assets.h"
static constexpr bool kEmbedded = true;
#else
// Built without the dashboard: the REST API still works.
static const char kPlaceholder[] =
    "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>PlantCare</title></head>"
    "<body style=\"font-family:system-ui;max-width:36rem;margin:3rem auto;padding:0 1rem\">"
    "<h1>PlantCare</h1><p>This firmware was built without the dashboard. Run "
    "<code>make dashboard</code> and rebuild, or use the REST API under <code>/api</code>.</p>"
    "</body></html>";
static const WebAsset kWebAssets[] = {
    {"/index.html", "text/html; charset=utf-8", reinterpret_cast<const uint8_t*>(kPlaceholder),
     sizeof(kPlaceholder) - 1, "\"placeholder\"", false, false},
};
static const size_t kWebAssetCount = 1;
static constexpr bool kEmbedded = false;
#endif

const WebAsset* findWebAsset(const char* path) {
  for (size_t i = 0; i < kWebAssetCount; ++i) {
    if (strcmp(kWebAssets[i].path, path) == 0) return &kWebAssets[i];
  }
  return nullptr;
}

bool webAssetsEmbedded() { return kEmbedded; }
