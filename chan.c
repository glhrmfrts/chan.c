#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <threads.h>
#include <stdatomic.h>

#ifndef chan_define_error
#define chan_define_error(e) e
#endif

enum {
    chan_success,
    chan_define_error(chan_timedout),
    chan_define_error(chan_closed),
    chan_define_error(chan_nomem),
    chan_define_error(chan_invalid),
    chan_define_error(chan_error),
};

struct chan_header {
    cnd_t cvar;
    mtx_t mtx;
    bool is_closed;
    size_t num_items;
    size_t items_top;
    size_t items_bottom;
};

#define chan_of(ValueType, max_items) struct { struct chan_header header; ValueType queue[max_items]; }

#define chan_init(ch) (chan_init_(&(ch)->header))

#define chan_send(ch, arg) (chan_send_(&(ch)->header, (arg), sizeof((ch)->queue), sizeof((ch)->queue[0])))

#define chan_receive(ch, dest) (chan_receive_(&(ch)->header, (dest), sizeof((ch)->queue), sizeof((ch)->queue[0])))

#define chan_close(ch) (chan_close_(&(ch)->header))

#define chan_destroy(ch) (chan_destroy_(&(ch)->header))

int chan_init_(struct chan_header* ch);

void chan_destroy_(struct chan_header* ch);

void chan_close_(struct chan_header* ch);

int chan_send_(struct chan_header* ch, void* arg, size_t queue_size, size_t elem_size);

int chan_receive_(struct chan_header* ch, void* arg, size_t queue_size, size_t elem_size);

#ifdef chan_implementation

int chan_init_(struct chan_header* ch) {
    memset(ch, 0, sizeof(struct chan_header));
    if (cnd_init(&ch->cvar)) {
        return chan_error;
    }
    if (mtx_init(&ch->mtx)) {
        return chan_error;
    }
    return chan_success;
}

void chan_destroy_(struct chan_header* ch) {
    chan_close_(ch);
    mtx_destroy(&ch->mtx);
    cnd_destroy(&ch->mtx);
}

void chan_close_(struct chan_header* ch) {
    mtx_lock(&ch->mtx);
    ch->is_closed = true;
    cnd_signal(&ch->cvar);
    mtx_unlock(&ch->mtx);
}

int chan_send_(struct chan_header* ch, void* arg, size_t queue_size, size_t elem_size) {
    int result = chan_success;

    void* queue = (void*)(ch + 1);
    size_t max_items = queue_size / elem_size;

    mtx_lock(&ch->mtx);
    if (ch->is_closed) {
        result = chan_closed;
        goto end;
    }

    while (ch->num_items == max_items) {
        cnd_wait(&ch->cvar, &ch->mtx);
        if (ch->is_closed) {
            result = chan_closed;
            goto end;
        }
    }

    void* item_to_write = (char*)queue + (ch->items_top*elem_size);
    memcpy(item_to_write, arg, elem_size);
    ch->items_top = (ch->items_top + 1) % max_items;
    ch->num_items++;
    cnd_signal(&ch->cvar);

end:
    mtx_unlock(&ch->mtx);
    return result;
}

int chan_receive_(struct chan_header* ch, void* arg, size_t queue_size, size_t elem_size) {
    int result = chan_success;

    void* queue = (void*)(ch + 1);
    size_t max_items = queue_size / elem_size;

    mtx_lock(&ch->mtx);
    if (ch->is_closed) {
        result = chan_closed;
        goto end;
    }

    while (ch->num_items == 0) {
        cnd_wait(&ch->cvar, &ch->mtx);
        if (ch->is_closed) {
            result = chan_closed;
            goto end;
        }
    }

    void* item_to_read = (char*)queue + (ch->items_bottom*elem_size);
    memcpy(arg, item_to_read, elem_size);
    ch->items_bottom = (ch->items_bottom + 1) % max_items;
    ch->num_items--;
    cnd_signal(&ch->cvar);

end:
    mtx_unlock(&ch->mtx);
    return result;
}

#endif

#ifdef chan_unittest

bool chan_runtest() {
    return false;
}

#endif