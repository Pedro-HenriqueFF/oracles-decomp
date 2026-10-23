#include "sync/update.h"
#include "sync/http.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool update_parse_latest(const char *json, char *tag, size_t size) {
  const char *p = strstr(json, "\"tag_name\"");
  if (!p) return false;
  p += strlen("\"tag_name\"");
  while (*p == ' ' || *p == ':') p++;
  if (*p++ != '"') return false;
  size_t n = 0;
  while (p[n] && p[n] != '"') {
    if (!isalnum((unsigned char)p[n]) && !strchr("._-", p[n])) return false;
    n++;
  }
  if (!n || p[n] != '"' || n >= size) return false;
  snprintf(tag, size, "%.*s", (int)n, p);
  return true;
}

bool update_check(const char *repo, const char *current, char *tag, size_t size) {
  char url[256];
  snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases/latest", repo);
  HttpResponse res = {0};
  bool ok = http_request("GET", url, "Accept: application/vnd.github+json\n", NULL, 0, &res) && res.status == 200 && res.body &&
            update_parse_latest((const char *)res.body, tag, size) && update_compare(tag, current) > 0;
  http_response_free(&res);
  return ok;
}
