#include "ywt-internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

struct ywt_buf {
    struct wl_buffer *wl;
    uint32_t         *data;
    int               w, h, stride;
    size_t            size;
};

static int create_shm_file(size_t size)
{
    int fd = -1;
    for (int i = 0; i < 16; i++) {
        char name[64];
        snprintf(name, sizeof(name), "/ywt-shm-%d-%d", (int)getpid(), i);
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) { shm_unlink(name); break; }
        if (errno != EEXIST) return -1;
    }
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static void buf_release(void *data, struct wl_buffer *wl)
{
    (void)data; (void)wl;
}

static const struct wl_buffer_listener buf_listener = {
    .release = buf_release,
};

ywt_buf_t *ywt_buf_create(ywt_surface_t *s, int w, int h)
{
    if (!s || w <= 0 || h <= 0) return NULL;
    struct wl_shm *shm = ywt_ctx_shm(ywt_surface_ctx(s));
    if (!shm) return NULL;

    ywt_buf_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->w = w;
    b->h = h;
    b->stride = w * 4;
    b->size = (size_t)b->stride * (size_t)h;

    int fd = create_shm_file(b->size);
    if (fd < 0) { free(b); return NULL; }

    b->data = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->data == MAP_FAILED) { close(fd); free(b); return NULL; }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)b->size);
    b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, b->stride,
                                      WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    if (!b->wl) {
        munmap(b->data, b->size);
        free(b);
        return NULL;
    }
    wl_buffer_add_listener(b->wl, &buf_listener, b);
    return b;
}

void ywt_buf_destroy(ywt_buf_t *b)
{
    if (!b) return;
    if (b->wl)   wl_buffer_destroy(b->wl);
    if (b->data) munmap(b->data, b->size);
    free(b);
}

struct wl_buffer *ywt_buf_wl(ywt_buf_t *b) { return b ? b->wl : NULL; }
uint32_t *ywt_buf_data(ywt_buf_t *b)   { return b ? b->data : NULL; }
int       ywt_buf_width(ywt_buf_t *b)  { return b ? b->w : 0; }
int       ywt_buf_height(ywt_buf_t *b) { return b ? b->h : 0; }
int       ywt_buf_stride(ywt_buf_t *b) { return b ? b->stride : 0; }

void ywt_buf_clear(ywt_buf_t *b, uint32_t argb)
{
    if (!b || !b->data) return;
    size_t n = (size_t)b->w * (size_t)b->h;
    for (size_t i = 0; i < n; i++) b->data[i] = argb;
}
