#include <caudio/media/source.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

/* ---------------------------------------------------------- */
/* Source base                                                */
/* ---------------------------------------------------------- */

ca_media_source_kind_t ca_media_source_kind(const ca_media_source* self) {
    return self ? self->ops->kind(self) : CA_MEDIA_SOURCE_PATH;
}

const char* ca_media_source_display_name(const ca_media_source* self) {
    return self ? self->ops->display_name(self) : nullptr;
}

bool ca_media_source_seekable(const ca_media_source* self) {
    return self ? self->ops->seekable(self) : false;
}

ca_status ca_media_source_open(ca_media_source* self,
                               ca_reader** out,
                               ca_error* err) {
    if (!self || !out) return CA_INVALID_ARGUMENT;
    return self->ops->open(self, out, err);
}

void ca_media_source_destroy(ca_media_source* self) {
    if (!self) return;
    self->ops->destroy(self);
}

/* ---------------------------------------------------------- */
/* Path source                                                */
/* ---------------------------------------------------------- */

struct ca_path_source {
    ca_media_source base;
    char* path;
    char* display_name;
};

static ca_media_source_kind_t path_source_kind(const ca_media_source* self) {
    (void)self;
    return CA_MEDIA_SOURCE_PATH;
}

static const char* path_source_display_name(const ca_media_source* self) {
    struct ca_path_source* impl = (struct ca_path_source*)self;
    return impl->display_name;
}

static bool path_source_seekable(const ca_media_source* self) {
    (void)self;
    return true;
}

static ca_status path_source_open(ca_media_source* self,
                                  ca_reader** out,
                                  ca_error* err) {
    struct ca_path_source* impl = (struct ca_path_source*)self;
    return ca_reader_file(out, impl->path, err);
}

static void path_source_destroy(ca_media_source* self) {
    if (!self) return;
    struct ca_path_source* impl = (struct ca_path_source*)self;
    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, impl->path, strlen(impl->path) + 1, 1);
    cx_allocator_free(alloc, impl->display_name, strlen(impl->display_name) + 1, 1);
    cx_allocator_free(alloc, impl, sizeof(struct ca_path_source), alignof(struct ca_path_source));
}

static const ca_media_source_ops path_source_ops = {
    .kind = path_source_kind,
    .display_name = path_source_display_name,
    .seekable = path_source_seekable,
    .open = path_source_open,
    .destroy = path_source_destroy,
};

ca_status ca_media_source_path(ca_media_source** out,
                               const char* path,
                               ca_error* err) {
    if (err) ca_error_init(err);
    if (!out || !path || path[0] == '\0') {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    struct ca_path_source* s = (struct ca_path_source*)cx_allocator_alloc(
        alloc, sizeof(struct ca_path_source), alignof(struct ca_path_source));
    if (!s) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    size_t path_len = strlen(path) + 1;
    s->path = (char*)cx_allocator_alloc(alloc, path_len, 1);
    if (!s->path) {
        cx_allocator_free(alloc, s, sizeof(struct ca_path_source), alignof(struct ca_path_source));
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    memcpy(s->path, path, path_len);

    s->display_name = (char*)cx_allocator_alloc(alloc, path_len, 1);
    if (!s->display_name) {
        cx_allocator_free(alloc, s->path, path_len, 1);
        cx_allocator_free(alloc, s, sizeof(struct ca_path_source), alignof(struct ca_path_source));
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    memcpy(s->display_name, path, path_len);

    s->base.ops = &path_source_ops;
    *out = (ca_media_source*)s;
    return CA_OK;
}

const char* ca_media_source_path_path(const ca_media_source* self) {
    if (!self) return nullptr;
    struct ca_path_source* impl = (struct ca_path_source*)self;
    return impl->path;
}

/* ---------------------------------------------------------- */
/* Descriptor source                                          */
/* ---------------------------------------------------------- */

struct ca_descriptor_source {
    ca_media_source base;
    int fd;
    bool readable;
    bool seekable;
    bool close_on_destroy;
    bool consumed;
    char* display_name;
};

static ca_media_source_kind_t desc_source_kind(const ca_media_source* self) {
    (void)self;
    return CA_MEDIA_SOURCE_DESCRIPTOR;
}

static const char* desc_source_display_name(const ca_media_source* self) {
    struct ca_descriptor_source* impl = (struct ca_descriptor_source*)self;
    return impl->display_name;
}

static bool desc_source_seekable(const ca_media_source* self) {
    struct ca_descriptor_source* impl = (struct ca_descriptor_source*)self;
    return impl->seekable;
}

static ca_status desc_source_open(ca_media_source* self,
                                  ca_reader** out,
                                  ca_error* err) {
    struct ca_descriptor_source* impl = (struct ca_descriptor_source*)self;
    if (impl->consumed) {
        if (err) ca_error_set(err, CA_NOT_SUPPORTED, "source already opened");
        return CA_NOT_SUPPORTED;
    }
    ca_status st = ca_reader_descriptor(out, impl->fd, impl->readable,
                                         impl->seekable, impl->close_on_destroy, err);
    if (st == CA_OK) {
        impl->consumed = true;
        impl->fd = -1;
        impl->close_on_destroy = false;
    }
    return st;
}

static void desc_source_destroy(ca_media_source* self) {
    if (!self) return;
    struct ca_descriptor_source* impl = (struct ca_descriptor_source*)self;
    const cx_allocator* alloc = cx_allocator_default();
    if (!impl->consumed && impl->close_on_destroy && impl->fd >= 0) {
#if defined(_WIN32)
        (void)_close(impl->fd);
#else
        (void)close(impl->fd);
#endif
    }
    cx_allocator_free(alloc, impl->display_name, strlen(impl->display_name) + 1, 1);
    cx_allocator_free(alloc, impl, sizeof(struct ca_descriptor_source), alignof(struct ca_descriptor_source));
}

static const ca_media_source_ops desc_source_ops = {
    .kind = desc_source_kind,
    .display_name = desc_source_display_name,
    .seekable = desc_source_seekable,
    .open = desc_source_open,
    .destroy = desc_source_destroy,
};

ca_status ca_media_source_descriptor(ca_media_source** out,
                                      int fd,
                                      bool readable,
                                      bool seekable,
                                      bool close_on_destroy,
                                      const char* display_name,
                                      ca_error* err) {
    if (err) ca_error_init(err);
    if (!out || fd < 0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    struct ca_descriptor_source* s = (struct ca_descriptor_source*)cx_allocator_alloc(
        alloc, sizeof(struct ca_descriptor_source), alignof(struct ca_descriptor_source));
    if (!s) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    const char* dn = display_name && display_name[0] ? display_name : nullptr;
    size_t dn_len;
    if (dn) {
        dn_len = strlen(dn) + 1;
    } else {
        /* "descriptor:<fd>" */
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "descriptor:%d", fd);
        dn_len = (size_t)n + 1;
    }

    s->fd = fd;
    s->readable = readable;
    s->seekable = seekable;
    s->close_on_destroy = close_on_destroy;
    s->consumed = false;

    s->display_name = (char*)cx_allocator_alloc(alloc, dn_len, 1);
    if (!s->display_name) {
        cx_allocator_free(alloc, s, sizeof(struct ca_descriptor_source), alignof(struct ca_descriptor_source));
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    if (dn) {
        memcpy(s->display_name, dn, dn_len);
    } else {
        snprintf(s->display_name, dn_len, "descriptor:%d", fd);
    }

    s->base.ops = &desc_source_ops;
    *out = (ca_media_source*)s;
    return CA_OK;
}

bool ca_media_source_consumed(const ca_media_source* self) {
    if (!self) return false;
    struct ca_descriptor_source* impl = (struct ca_descriptor_source*)self;
    return impl->consumed;
}
