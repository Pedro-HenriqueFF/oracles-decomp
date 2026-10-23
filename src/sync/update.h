#pragma once
// Whether a newer release is out: the repository's latest GitHub release (pre-releases never count)
// against the version this build was made from. A build without a version is older than any release.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char tag[16];                 // "v1.2.0"
  char url[256];                // this system's file in the release, or empty when it has none
  char sha256[65];              // that file's SHA-256 (GitHub's digest), or empty
} UpdateRelease;

// <0, 0 or >0 as a is older than, the same as, or newer than b. "v1.2.3", "1.2" and "" (0.0.0) all parse.
int update_compare(const char *a, const char *b);
// A releases/latest response: its tag (false without one fit for a URL), and the url and digest of
// the file named asset.
bool update_parse_latest(const char *json, const char *asset, UpdateRelease *r);
// Blocking (run it off the game's thread): fetches api_url (a releases/latest) and is true when that
// release is newer than current.
bool update_check(const char *api_url, const char *current, const char *asset, UpdateRelease *r);

#ifndef _WIN32
// Replaces the AppImage at path with data, an ELF file whose SHA-256 must be sha256: written beside
// it, made executable, then renamed over it, so a failure leaves the old one as it was.
bool update_install(const char *path, const uint8_t *data, size_t size, const char *sha256, char *err, size_t esize);
#endif
