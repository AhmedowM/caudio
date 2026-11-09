#include <caudio/media/reader.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#else
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#endif

/* ---------------------------------------------------------- */
/* 64-bit file helpers                                        */
/* ---------------------------------------------------------- */

#if defined(_WIN32)
static int64_t ftell64(FILE* f) {
    return _ftelli64(f);
}
static int fseek64(FILE* f, int64_t off, int whence) {
    return _fseeki64(f, off, whence);
}
#else
static int64_t ftell64(FILE* f) {
    off_t o = ftello(f);
    if (o == (off_t)-1) return -1;
    return (int64_t)o;
}
static int fseek64(FILE* f, int64_t off, int whence) {
    return fseeko(f, (off_t)off, whence);
}
#endif

/* ---------------------------------------------------------- */
/* File reader                                                */
/* ---------------------------------------------------------- */

struct ca_reader_file {
    ca_reader base;   /* Must be first */
    FILE* file;
    int64_t file_size;
};

static size_t file_reader_read(ca_reader* self, uint8_t* dst, size_t len) {
    if (!self || !dst || len == 0) return 0;
    struct ca_reader_file* impl = (struct ca_reader_file*)self;
    if (!impl->file) return 0;
    return fread(dst, 1, len, impl->file);
}

static ca_status file_reader_seek(ca_reader* self, int64_t offset, int whence) {
    if (!self) return CA_INVALID_ARGUMENT;
    struct ca_reader_file* impl = (struct ca_reader_file*)self;
    if (!impl->file) return CA_INVALID_ARGUMENT;
    if (fseek64(impl->file, offset, whence) != 0) return CA_IO_ERROR;
    return CA_OK;
}

static int64_t file_reader_tell(ca_reader* self) {
    if (!self) return -1;
    struct ca_reader_file* impl = (struct ca_reader_file*)self;
    if (!impl->file) return -1;
    return ftell64(impl->file);
}

static int64_t file_reader_size(ca_reader* self) {
    if (!self) return -1;
    struct ca_reader_file* impl = (struct ca_reader_file*)self;
    return impl->file_size;
}

static bool file_reader_seekable(const ca_reader* self) {
    (void)self;
    return true;
}

static void file_reader_destroy(ca_reader* self) {
    if (!self) return;
    struct ca_reader_file* impl = (struct ca_reader_file*)self;
    if (impl->file) fclose(impl->file);
    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, impl, sizeof(struct ca_reader_file), alignof(struct ca_reader_file));
}

static const ca_reader_ops file_reader_ops = {
    .read = file_reader_read,
    .seek = file_reader_seek,
    .tell = file_reader_tell,
    .size = file_reader_size,
    .seekable = file_reader_seekable,
    .destroy = file_reader_destroy,
};

ca_status ca_reader_file(ca_reader** out, const char* path, ca_error* err) {
    if (err) ca_error_init(err);
    if (!out || !path || path[0] == '\0') {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32)
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (err) ca_error_set(err, CA_NOT_FOUND, "cannot open file");
        return CA_NOT_FOUND;
    }
#else
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (err) ca_error_set(err, CA_NOT_FOUND, "cannot open file");
        return CA_NOT_FOUND;
    }
#endif

    if (fseek64(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err) ca_error_set(err, CA_IO_ERROR, "fseek failed");
        return CA_IO_ERROR;
    }
    int64_t sz = ftell64(f);
    if (sz < 0) {
        fclose(f);
        if (err) ca_error_set(err, CA_IO_ERROR, "ftell failed");
        return CA_IO_ERROR;
    }
    if (fseek64(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (err) ca_error_set(err, CA_IO_ERROR, "fseek failed");
        return CA_IO_ERROR;
    }

    const cx_allocator* alloc = cx_allocator_default();
    struct ca_reader_file* impl = (struct ca_reader_file*)cx_allocator_alloc(
        alloc, sizeof(struct ca_reader_file), alignof(struct ca_reader_file));
    if (!impl) {
        fclose(f);
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    impl->base.ops = &file_reader_ops;
    impl->file = f;
    impl->file_size = sz;

    *out = (ca_reader*)impl;
    return CA_OK;
}

/* ---------------------------------------------------------- */
/* Memory reader                                              */
/* ---------------------------------------------------------- */

struct ca_reader_memory {
    ca_reader base;
    uint8_t* buf;
    size_t len;
    size_t pos;
};

static size_t mem_reader_read(ca_reader* self, uint8_t* dst, size_t len) {
    if (!self || !dst || len == 0) return 0;
    struct ca_reader_memory* impl = (struct ca_reader_memory*)self;
    if (impl->pos >= impl->len) return 0;
    size_t avail = impl->len - impl->pos;
    size_t n = len < avail ? len : avail;
    memcpy(dst, impl->buf + impl->pos, n);
    impl->pos += n;
    return n;
}

static ca_status mem_reader_seek(ca_reader* self, int64_t offset, int whence) {
    if (!self) return CA_INVALID_ARGUMENT;
    struct ca_reader_memory* impl = (struct ca_reader_memory*)self;
    int64_t base = 0;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = (int64_t)impl->pos; break;
    case SEEK_END: base = (int64_t)impl->len; break;
    default: return CA_INVALID_ARGUMENT;
    }
    int64_t new_pos = base + offset;
    if (new_pos < 0 || new_pos > (int64_t)impl->len) return CA_INVALID_ARGUMENT;
    impl->pos = (size_t)new_pos;
    return CA_OK;
}

static int64_t mem_reader_tell(ca_reader* self) {
    if (!self) return -1;
    struct ca_reader_memory* impl = (struct ca_reader_memory*)self;
    return (int64_t)impl->pos;
}

static int64_t mem_reader_size(ca_reader* self) {
    if (!self) return -1;
    struct ca_reader_memory* impl = (struct ca_reader_memory*)self;
    return (int64_t)impl->len;
}

static bool mem_reader_seekable(const ca_reader* self) {
    (void)self;
    return true;
}

static void mem_reader_destroy(ca_reader* self) {
    if (!self) return;
    struct ca_reader_memory* impl = (struct ca_reader_memory*)self;
    const cx_allocator* alloc = cx_allocator_default();
    if (impl->buf) {
        cx_allocator_free(alloc, impl->buf, impl->len, 1);
    }
    cx_allocator_free(alloc, impl, sizeof(struct ca_reader_memory), alignof(struct ca_reader_memory));
}

static const ca_reader_ops mem_reader_ops = {
    .read = mem_reader_read,
    .seek = mem_reader_seek,
    .tell = mem_reader_tell,
    .size = mem_reader_size,
    .seekable = mem_reader_seekable,
    .destroy = mem_reader_destroy,
};

ca_status ca_reader_memory(ca_reader** out, const uint8_t* data, size_t len, ca_error* err) {
    if (err) ca_error_init(err);
    if (!out) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        return CA_INVALID_ARGUMENT;
    }
    if (len > 0 && !data) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "null data with non-zero length");
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    struct ca_reader_memory* impl = (struct ca_reader_memory*)cx_allocator_alloc(
        alloc, sizeof(struct ca_reader_memory), alignof(struct ca_reader_memory));
    if (!impl) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    impl->base.ops = &mem_reader_ops;
    impl->len = len;
    impl->pos = 0;
    if (len > 0) {
        impl->buf = (uint8_t*)cx_allocator_alloc(alloc, len, 1);
        if (!impl->buf) {
            cx_allocator_free(alloc, impl, sizeof(struct ca_reader_memory), alignof(struct ca_reader_memory));
            if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
            return CA_OUT_OF_MEMORY;
        }
        memcpy(impl->buf, data, len);
    } else {
        impl->buf = nullptr;
    }

    *out = (ca_reader*)impl;
    return CA_OK;
}

/* ---------------------------------------------------------- */
/* Descriptor reader                                          */
/* ---------------------------------------------------------- */

#if defined(_WIN32)
static int64_t fd_tell64(int fd) {
    return _lseeki64(fd, 0, SEEK_CUR);
}
static int64_t fd_seek64(int fd, int64_t off, int whence) {
    return _lseeki64(fd, off, whence);
}
static int64_t fd_size(int fd) {
    struct _stat64 st;
    memset(&st, 0, sizeof(st));
    if (_fstat64(fd, &st) != 0) return -1;
    return (int64_t)st.st_size;
}
static int64_t fd_read(int fd, uint8_t* dst, size_t len) {
    unsigned int chunk = (unsigned int)(len > (size_t)INT_MAX ? INT_MAX : len);
    return (int64_t)_read(fd, dst, chunk);
}
static void fd_close(int fd) {
    (void)_close(fd);
}
#else
static int64_t fd_tell64(int fd) {
    off_t o = lseek(fd, 0, SEEK_CUR);
    if (o == (off_t)-1) return -1;
    return (int64_t)o;
}
static int64_t fd_seek64(int fd, int64_t off, int whence) {
    off_t o = lseek(fd, (off_t)off, whence);
    if (o == (off_t)-1) return -1;
    return (int64_t)o;
}
static int64_t fd_size(int fd) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (fstat(fd, &st) != 0) return -1;
    return (int64_t)st.st_size;
}
static int64_t fd_read(int fd, uint8_t* dst, size_t len) {
    return (int64_t)read(fd, dst, len);
}
static void fd_close(int fd) {
    (void)close(fd);
}
#endif

struct ca_reader_descriptor {
    ca_reader base;
    int fd;
    bool seekable;
    bool owned;
    int64_t size;
    int64_t pos;
};

static size_t desc_reader_read(ca_reader* self, uint8_t* dst, size_t len) {
    if (!self || !dst || len == 0) return 0;
    struct ca_reader_descriptor* impl = (struct ca_reader_descriptor*)self;
    if (impl->fd < 0) return 0;
    for (;;) {
        int64_t n = fd_read(impl->fd, dst, len);
        if (n < 0) {
#if defined(_WIN32)
            return 0;
#else
            if (errno == EINTR) continue;
            return 0;
#endif
        }
        impl->pos += n;
        return (size_t)n;
    }
}

static ca_status desc_reader_seek(ca_reader* self, int64_t offset, int whence) {
    if (!self) return CA_INVALID_ARGUMENT;
    struct ca_reader_descriptor* impl = (struct ca_reader_descriptor*)self;
    if (impl->fd < 0) return CA_INVALID_ARGUMENT;
    if (!impl->seekable) return CA_NOT_SUPPORTED;

    int64_t base = 0;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = impl->pos; break;
    case SEEK_END:
        if (impl->size < 0) return CA_NOT_SUPPORTED;
        base = impl->size;
        break;
    default: return CA_INVALID_ARGUMENT;
    }
    int64_t new_pos = base + offset;
    if (new_pos < 0 || (impl->size >= 0 && new_pos > impl->size)) return CA_INVALID_ARGUMENT;
    int64_t landed = fd_seek64(impl->fd, new_pos, SEEK_SET);
    if (landed < 0) return CA_IO_ERROR;
    impl->pos = landed;
    return CA_OK;
}

static int64_t desc_reader_tell(ca_reader* self) {
    if (!self) return -1;
    struct ca_reader_descriptor* impl = (struct ca_reader_descriptor*)self;
    if (impl->fd < 0) return -1;
    return impl->pos;
}

static int64_t desc_reader_size(ca_reader* self) {
    if (!self) return -1;
    struct ca_reader_descriptor* impl = (struct ca_reader_descriptor*)self;
    return impl->size;
}

static bool desc_reader_seekable(const ca_reader* self) {
    if (!self) return false;
    return ((struct ca_reader_descriptor*)self)->seekable;
}

static void desc_reader_destroy(ca_reader* self) {
    if (!self) return;
    struct ca_reader_descriptor* impl = (struct ca_reader_descriptor*)self;
    const cx_allocator* alloc = cx_allocator_default();
    if (impl->owned && impl->fd >= 0) {
        fd_close(impl->fd);
    }
    cx_allocator_free(alloc, impl, sizeof(struct ca_reader_descriptor), alignof(struct ca_reader_descriptor));
}

static const ca_reader_ops desc_reader_ops = {
    .read = desc_reader_read,
    .seek = desc_reader_seek,
    .tell = desc_reader_tell,
    .size = desc_reader_size,
    .seekable = desc_reader_seekable,
    .destroy = desc_reader_destroy,
};

ca_status ca_reader_descriptor(ca_reader** out,
                               int fd,
                               bool readable,
                               bool seekable,
                               bool close_on_destroy,
                               ca_error* err) {
    if (err) ca_error_init(err);
    if (!out) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        return CA_INVALID_ARGUMENT;
    }
    if (fd < 0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid descriptor");
        return CA_INVALID_ARGUMENT;
    }
    if (!readable) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "descriptor is not readable");
        return CA_INVALID_ARGUMENT;
    }

    int64_t size = -1;
    int64_t pos = 0;
    if (seekable) {
        pos = fd_tell64(fd);
        if (pos < 0) {
            if (err) ca_error_set(err, CA_NOT_SUPPORTED, "descriptor position unavailable");
            return CA_NOT_SUPPORTED;
        }
        size = fd_size(fd);
    }

    const cx_allocator* alloc = cx_allocator_default();
    struct ca_reader_descriptor* impl = (struct ca_reader_descriptor*)cx_allocator_alloc(
        alloc, sizeof(struct ca_reader_descriptor), alignof(struct ca_reader_descriptor));
    if (!impl) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    impl->base.ops = &desc_reader_ops;
    impl->fd = fd;
    impl->seekable = seekable;
    impl->owned = close_on_destroy;
    impl->size = size;
    impl->pos = pos;

    *out = (ca_reader*)impl;
    return CA_OK;
}

size_t ca_reader_read(ca_reader* self, uint8_t* dst, size_t len) {
    if (!self || !self->ops || !self->ops->read) return 0;
    return self->ops->read(self, dst, len);
}

ca_status ca_reader_seek(ca_reader* self, int64_t offset, int whence) {
    if (!self || !self->ops || !self->ops->seek) return CA_INVALID_ARGUMENT;
    return self->ops->seek(self, offset, whence);
}

int64_t ca_reader_tell(ca_reader* self) {
    if (!self || !self->ops || !self->ops->tell) return -1;
    return self->ops->tell(self);
}

int64_t ca_reader_size(ca_reader* self) {
    if (!self || !self->ops || !self->ops->size) return -1;
    return self->ops->size(self);
}

bool ca_reader_seekable(const ca_reader* self) {
    if (!self || !self->ops || !self->ops->seekable) return false;
    return self->ops->seekable(self);
}

void ca_reader_destroy(ca_reader* self) {
    if (!self) return;
    if (self->ops && self->ops->destroy) self->ops->destroy(self);
}
