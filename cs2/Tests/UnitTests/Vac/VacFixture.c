// Fake steamclient for the VAC harness (VacHarness.cpp).
//
// Every libc call below resolves through THIS module's PLT/GOT - exactly how
// steamclient.so's own calls resolve in-game. The harness patches this
// module's GOT with the production hook addresses, so these helpers exercise
// the real filter/spoof code paths end to end. Callers must go through these
// helpers (never libc directly): a direct call from the test binary uses the
// test binary's own unpatched PLT and bypasses the hooks, which is precisely
// the distinction under test.
//
// Build output name must keep containing "steamclient.so": the production
// resolver locates its target with strstr(l_name, "steamclient.so").
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__GNUC__)
#define VACF_API __attribute__((visibility("default")))
#else
#define VACF_API
#endif

// Read a whole text file via fopen/fgets with the given per-call buffer size.
// Small sizes force line splits across fgets calls. Returns malloc'd
// NUL-terminated content (*out_len excludes the NUL), or NULL on failure.
VACF_API char* vacf_fgets_all(const char* path, int bufsz, size_t* out_len)
{
    if (!path || bufsz <= 1 || !out_len)
        return NULL;
    *out_len = 0;

    FILE* fp = fopen(path, "r");
    if (!fp)
        return NULL;

    char* chunk = (char*)malloc((size_t)bufsz);
    char* acc = NULL;
    size_t acc_len = 0;
    if (!chunk) {
        fclose(fp);
        return NULL;
    }

    char* r;
    while ((r = fgets(chunk, bufsz, fp)) != NULL) {
        size_t n = strlen(chunk);
        char* grown = (char*)realloc(acc, acc_len + n + 1);
        if (!grown) {
            free(acc);
            acc = NULL;
            acc_len = 0;
            break;
        }
        acc = grown;
        memcpy(acc + acc_len, chunk, n + 1);
        acc_len += n;
    }
    free(chunk);
    fclose(fp);

    if (!acc) {
        acc = (char*)malloc(1);
        if (acc)
            acc[0] = '\0';
    }
    *out_len = acc_len;
    return acc;
}

// Read a whole file via open/read with the given per-call chunk size. Small
// chunks force hide tokens to split across read() boundaries - the stash
// logic's exact case. Returns malloc'd content (*out_len bytes), or NULL.
VACF_API char* vacf_read_all(const char* path, size_t chunk, size_t* out_len)
{
    if (!path || chunk == 0 || !out_len)
        return NULL;
    *out_len = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    char* acc = NULL;
    size_t acc_len = 0;
    for (;;) {
        char* grown = (char*)realloc(acc, acc_len + chunk);
        if (!grown) {
            free(acc);
            acc = NULL;
            acc_len = 0;
            break;
        }
        acc = grown;
        ssize_t n = read(fd, acc + acc_len, chunk);
        if (n < 0) {
            free(acc);
            acc = NULL;
            acc_len = 0;
            break;
        }
        if (n == 0)
            break;
        acc_len += (size_t)n;
    }
    close(fd);

    if (!acc) {
        acc = (char*)malloc(1);
    }
    *out_len = acc_len;
    return acc;
}

// fopen+fread path (what a file-hashing module would do first).
VACF_API int vacf_fopen_first(const char* path, unsigned char* out, size_t n, size_t* got)
{
    if (!path || !out || n == 0 || !got)
        return -1;
    *got = 0;
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return -1;
    *got = fread(out, 1, n, fp);
    int rc = ferror(fp) ? -1 : 0;
    fclose(fp);
    return rc;
}

// open+read path.
VACF_API ssize_t vacf_open_read(const char* path, void* buf, size_t n)
{
    if (!path || !buf)
        return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, n);
    close(fd);
    return r;
}

// openat path: modern glibc fopen() opens via openat, so the file-spoof must
// also trigger here, and maps enumeration via openat must be tracked.
VACF_API ssize_t vacf_openat_read(const char* path, void* buf, size_t n)
{
    if (!path || !buf)
        return -1;
    int fd = openat(AT_FDCWD, path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, n);
    close(fd);
    return r;
}

// pread path (offset reads must see the same fake content).
VACF_API ssize_t vacf_pread_first(const char* path, void* buf, size_t n, off_t off)
{
    if (!path || !buf)
        return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t r = pread(fd, buf, n, off);
    close(fd);
    return r;
}

// fstat probe: a pipe-backed spoof reports S_FIFO here; memfd reports S_REG.
VACF_API int vacf_fstat_probe(const char* path, long* size_out, int* isreg_out)
{
    if (!path || !size_out || !isreg_out)
        return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat st;
    int rc = fstat(fd, &st);
    if (rc == 0) {
        *size_out = (long)st.st_size;
        *isreg_out = S_ISREG(st.st_mode) ? 1 : 0;
    }
    close(fd);
    return rc;
}

// mmap probe: a pipe-backed spoof fails here; memfd maps fine.
VACF_API int vacf_mmap_first(const char* path, unsigned char first4[4])
{
    if (!path || !first4)
        return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat st;
    int rc = -1;
    if (fstat(fd, &st) == 0 && st.st_size >= 4) {
        void* m = mmap(NULL, 4, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m != MAP_FAILED) {
            memcpy(first4, m, 4);
            rc = munmap(m, 4);
        }
    }
    close(fd);
    return rc;
}

// lseek-to-end size probe.
VACF_API int vacf_lseek_size(const char* path, long* end_out)
{
    if (!path || !end_out)
        return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    off_t e = lseek(fd, 0, SEEK_END);
    int rc = (e == (off_t)-1) ? -1 : 0;
    if (rc == 0)
        *end_out = (long)e;
    close(fd);
    return rc;
}
