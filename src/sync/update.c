#include "sync/update.h"
#include "sync/http.h"
#include "sync/sha256.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void parse(const char *s, long v[3]) {
  v[0] = v[1] = v[2] = 0;
  if (*s == 'v' || *s == 'V') s++;
  for (int i = 0; i < 3 && isdigit((unsigned char)*s); i++) {
    char *end;
    v[i] = strtol(s, &end, 10);
    if (*end != '.') break;
    s = end + 1;
  }
}

int update_compare(const char *a, const char *b) {
  long va[3], vb[3];
  parse(a, va);
  parse(b, vb);
  for (int i = 0; i < 3; i++)
    if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
  return 0;
}

// The string value of the first "key" in p (before end, when end is set). allowed: the characters
// it may hold; false when it holds another, is empty, or does not fit.
static bool string_of(const char *p, const char *end, const char *key, const char *allowed, char *out, size_t size, const char **after) {
  char quoted[32];
  snprintf(quoted, sizeof quoted, "\"%s\"", key);
  p = strstr(p, quoted);
  if (!p || (end && p >= end)) return false;
  p += strlen(quoted);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ':') p++;
  if (*p++ != '"') return false;
  size_t n = 0;
  while (p[n] && p[n] != '"') {
    if (allowed ? !isalnum((unsigned char)p[n]) && !strchr(allowed, p[n]) : p[n] == '\\' || (unsigned char)p[n] <= ' ') return false;
    n++;
  }
  if (!n || p[n] != '"' || n >= size) return false;
  snprintf(out, size, "%.*s", (int)n, p);
  if (after) *after = p + n + 1;
  return true;
}

bool update_parse_latest(const char *json, const char *asset, UpdateRelease *r) {
  memset(r, 0, sizeof *r);
  if (!string_of(json, NULL, "tag_name", "._-", r->tag, sizeof r->tag, NULL)) return false;
  // an asset's object: its name, then (no other "name" in between) its digest and its download URL
  const char *p = json, *at = NULL;
  char name[128];
  while (!at && (p = strstr(p, "\"name\"")))
    if (!string_of(p, NULL, "name", NULL, name, sizeof name, &p)) p += 6;
    else if (!strcmp(name, asset)) at = p;
  if (!at) return true;
  const char *next = strstr(at, "\"name\"");
  char digest[80];
  string_of(at, next, "browser_download_url", NULL, r->url, sizeof r->url, NULL);
  if (string_of(at, next, "digest", ":", digest, sizeof digest, NULL) && !strncmp(digest, "sha256:", 7) && strlen(digest) == 7 + 64)
    memcpy(r->sha256, digest + 7, 65);
  return true;
}

bool update_check(const char *api_url, const char *current, const char *asset, UpdateRelease *r) {
  HttpResponse res = {0};
  bool ok = http_request("GET", api_url, "Accept: application/vnd.github+json\n", NULL, 0, &res) && res.status == 200 && res.body &&
            update_parse_latest((const char *)res.body, asset, r) && update_compare(r->tag, current) > 0;
  http_response_free(&res);
  return ok;
}

#ifndef _WIN32
bool update_install(const char *path, const uint8_t *data, size_t size, const char *sha256, char *err, size_t esize) {
  char sha[65], tmp[1100];
  if (size < 4 || memcmp(data, "\x7f" "ELF", 4)) { snprintf(err, esize, "not an AppImage"); return false; }
  sha256_hex(data, size, sha);
  if (!sha256 || strcmp(sha, sha256)) { snprintf(err, esize, "download damaged"); return false; }
  struct stat st;
  mode_t mode = stat(path, &st) == 0 ? (st.st_mode & 07777) | 0111 : 0755;
  snprintf(tmp, sizeof tmp, "%s.new", path);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0700);
  bool ok = fd >= 0;
  for (size_t done = 0; ok && done < size;) {
    ssize_t n = write(fd, data + done, size - done);
    if (n < 0 && errno == EINTR) continue;
    ok = n > 0;
    if (ok) done += (size_t)n;
  }
  ok = ok && fsync(fd) == 0;
  if (fd >= 0 && close(fd) != 0) ok = false;
  ok = ok && chmod(tmp, mode) == 0 && rename(tmp, path) == 0;
  if (!ok) {
    snprintf(err, esize, "%s", strerror(errno));
    if (fd >= 0) unlink(tmp);
  }
  return ok;
}
#endif
