/* Native driver for dist/test_dist.py: the one-file Setup's portable decisions (setup-core.h) and SHA-256 (sha256.h).
 *   setup-core-test sha <file>            prints the file's SHA-256
 *   setup-core-test parse <blob>          prints the payload's file count and names, or -1
 *   setup-core-test verify <blob> <hex>   prints 1 when the blob matches the checksum, else 0
 *   setup-core-test zip <exe> <temp>      prints 1 when the exe path is "run from inside a zip" */
#include <stdio.h>
#include <stdlib.h>
#include "setup-core.h"

static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f) exit(2);
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    *len = (size_t)n;
    return b;
}

static void widen(const char *s, wchar_t *out, size_t cap)
{
    size_t i;
    for (i = 0; s[i] && i + 1 < cap; i++) out[i] = s[i] == '/' ? L'\\' : (wchar_t)(unsigned char)s[i];
    out[i] = 0;
}

int main(int argc, char **argv)
{
    size_t len;
    if (argc == 3 && !strcmp(argv[1], "sha")) {
        char hex[65];
        unsigned char *b = slurp(argv[2], &len);
        sha256_hex(b, len, hex);
        printf("%s\n", hex);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "parse")) {
        static struct sc_file files[SC_MAX_FILES];
        unsigned char *b = slurp(argv[2], &len);
        int n = sc_parse(b, len, files), i;
        printf("%d\n", n);
        for (i = 0; i < n; i++) printf("%s %u\n", files[i].name, files[i].size);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "verify")) {
        unsigned char *b = slurp(argv[2], &len);
        printf("%d\n", sc_verify(b, len, (const unsigned char *)argv[3], strlen(argv[3])));
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "zip")) {
        wchar_t exe[512], temp[512];
        widen(argv[2], exe, 512); widen(argv[3], temp, 512);
        printf("%d\n", sc_run_from_zip(exe, temp));
        return 0;
    }
    return 2;
}
