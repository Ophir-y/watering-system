// The dashboard, compiled into the firmware as gzipped byte arrays by
// dashboard/scripts/embed-assets.mjs (src/generated/web_assets.h).
#pragma once

#include <stddef.h>
#include <stdint.h>

struct WebAsset {
  const char* path;
  const char* contentType;
  const uint8_t* data;
  size_t length;
  const char* etag;
  bool gzip;
  bool immutable;  // content-hashed file name: cache forever
};

const WebAsset* findWebAsset(const char* path);
bool webAssetsEmbedded();
