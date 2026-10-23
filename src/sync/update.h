#pragma once
// Whether a newer release is out: the repository's latest GitHub release (pre-releases never count)
// against the version this build was made from. A build without a version is older than any release.
#include <stdbool.h>
#include <stddef.h>

// <0, 0 or >0 as a is older than, the same as, or newer than b. "v1.2.3", "1.2" and "" (0.0.0) all parse.
int update_compare(const char *a, const char *b);
// The tag_name of a releases/latest response ("v1.2.0"); false when it has none fit for a URL.
bool update_parse_latest(const char *json, char *tag, size_t size);
// Blocking (run it off the game's thread): true with the latest release's tag in tag when it is
// newer than current.
bool update_check(const char *repo, const char *current, char *tag, size_t size);
