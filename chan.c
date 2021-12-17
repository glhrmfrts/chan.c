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

#ifndef define_error
#define define_error(e) e
#endif

enum {
    chan_success,
    define_error(chan_timedout),
    define_error(chan_closed),
    define_error(chan_nomem),
    define_error(chan_invalid),
    define_error(chan_invalidarg),
    define_error(chan_error),
};

struct chan_header {
    cnd_t cvar;
    mtx_t mtx;
    bool is_closed;
    size_t queue_size;
    size_t value_size;
    size_t num_items;
    size_t items_top;
    size_t items_bottom;
};

#define chan_of(ValueType, max_items) struct { struct chan_header header; ValueType queue[max_items]; ValueType in_value; }

#define chan_new(T) ((T*)chan_new_(sizeof(T), sizeof(((T*)0)->queue), sizeof(((T*)0)->queue[0])))

#define chan_init(ch) (chan_init_(&(ch)->header, sizeof((ch)->queue), sizeof((ch)->queue[0])))

#define chan_send(ch, arg) (((ch)->in_value=(arg), chan_send_(&(ch)->header, (void*)(&(ch)->in_value), sizeof(arg), 0, false)))

#define chan_sendtimed(ch, timeout_ms, arg) (((ch)->in_value=(arg), chan_send_(&(ch)->header, (void*)(&(ch)->in_value), sizeof(arg), (timeout_ms), true)))

#define chan_receive(ch, dest) (chan_receive_(&(ch)->header, (void*)(dest), sizeof(*(dest)=(ch)->queue[0]), 0, false))

#define chan_receivetimed(ch, timeout_ms, dest) (chan_receive_(&(ch)->header, (void*)(dest), sizeof(*(dest)=(ch)->queue[0]), (timeout_ms), false))

#define chan_close(ch) (chan_close_(&(ch)->header))

#define chan_destroy(ch) (chan_destroy_(&(ch)->header))

#define chan_delete(ch) (chan_delete_(&(ch)->header))

struct chan_header* chan_new_(size_t type_size, size_t queue_size, size_t value_size);

int chan_init_(struct chan_header* ch, size_t queue_size, size_t value_size);

void chan_destroy_(struct chan_header* ch);

void chan_delete_(struct chan_header* ch);

void chan_close_(struct chan_header* ch);

int chan_send_(struct chan_header* ch, void* arg, size_t arg_size, int32_t timeout_ms, bool timeout);

int chan_receive_(struct chan_header* ch, void* arg, size_t arg_size, int32_t timeout_ms, bool timeout);

const char* chan_errorstr(int err);

#ifdef chan_implementation

struct chan_header* chan_new_(size_t type_size, size_t queue_size, size_t value_size) {
    struct chan_header* ch = malloc(type_size);
    if (chan_init_(ch, queue_size, value_size)) {
        free(ch);
        return NULL;
    }
    return ch;
}

int chan_init_(struct chan_header* ch, size_t queue_size, size_t value_size) {
    memset(ch, 0, sizeof(struct chan_header));
    ch->queue_size = queue_size;
    ch->value_size = value_size;
    if (cnd_init(&ch->cvar)) {
        return chan_error;
    }
    if (mtx_init(&ch->mtx, mtx_plain)) {
        return chan_error;
    }
    return chan_success;
}

void chan_destroy_(struct chan_header* ch) {
    chan_close_(ch);
    mtx_destroy(&ch->mtx);
    cnd_destroy(&ch->cvar);
}

void chan_delete_(struct chan_header* ch) {
    chan_destroy_(ch);
    free(ch);
}

void chan_close_(struct chan_header* ch) {
    mtx_lock(&ch->mtx);
    ch->is_closed = true;
    cnd_signal(&ch->cvar);
    mtx_unlock(&ch->mtx);
}

int chan_send_(struct chan_header* ch, void* arg, size_t arg_size, int32_t timeout_ms, bool timeout) {
    if (arg_size != ch->value_size) {
        return chan_invalidarg;
    }

    int result = chan_success;

    void* queue = (void*)(ch + 1);
    size_t max_items = ch->queue_size / ch->value_size;

    mtx_lock(&ch->mtx);
    if (ch->is_closed) {
        result = chan_closed;
        goto end;
    }

    while (ch->num_items == max_items) {
        if (timeout) {
            if (timeout_ms == 0) {
                result = chan_timedout;
                goto end;
            } else {
                struct timespec start, end;
                timespec_get(&start, TIME_UTC);

                end.tv_sec = start.tv_sec + (timeout_ms / 1000);
                end.tv_nsec = (timeout_ms % 1000) * 1000000;

                int err = cnd_timedwait(&ch->cvar, &ch->mtx, &end);
                if (err == thrd_timedout) {
                    result = chan_timedout;
                    goto end;
                } else if (err) {
                    result = chan_error;
                    goto end;
                }
            }
        } else {
            cnd_wait(&ch->cvar, &ch->mtx);
        }

        if (ch->is_closed) {
            result = chan_closed;
            goto end;
        }
    }

    void* item_to_write = (char*)queue + (ch->items_top*ch->value_size);
    memcpy(item_to_write, arg, ch->value_size);
    ch->items_top = (ch->items_top + 1) % max_items;
    ch->num_items++;
    cnd_signal(&ch->cvar);

end:
    mtx_unlock(&ch->mtx);
    return result;
}

int chan_receive_(struct chan_header* ch, void* arg, size_t arg_size, int32_t timeout_ms, bool timeout) {
    if (arg_size != ch->value_size) {
        return chan_invalidarg;
    }

    int result = chan_success;

    void* queue = (void*)(ch + 1);
    size_t max_items = ch->queue_size / ch->value_size;

    //printf("max_items: %zu; queue_size: %zu; elem_size: %zu\n", max_items, ch->queue_size, ch->value_size);

    mtx_lock(&ch->mtx);
    if (ch->is_closed) {
        result = chan_closed;
        goto end;
    }

    while (ch->num_items == 0) {
        if (timeout) {
            if (timeout_ms == 0) {
                result = chan_timedout;
                goto end;
            } else {
                struct timespec start, end;
                timespec_get(&start, TIME_UTC);

                end.tv_sec = start.tv_sec + (timeout_ms / 1000);
                end.tv_nsec = (timeout_ms % 1000) * 1000000;

                int err = cnd_timedwait(&ch->cvar, &ch->mtx, &end);
                if (err == thrd_timedout) {
                    result = chan_timedout;
                    goto end;
                } else if (err) {
                    result = chan_error;
                    goto end;
                }
            }
        } else {
            cnd_wait(&ch->cvar, &ch->mtx);
        }

        if (ch->is_closed) {
            result = chan_closed;
            goto end;
        }
    }

    void* item_to_read = (char*)queue + (ch->items_bottom*ch->value_size);
    memcpy(arg, item_to_read, ch->value_size);
    ch->items_bottom = (ch->items_bottom + 1) % max_items;
    ch->num_items--;
    cnd_signal(&ch->cvar);

end:
    mtx_unlock(&ch->mtx);
    return result;
}

const char* chan_errorstr(int err) {
    switch (err) {
    case chan_success: return "chan_success: Success";
    case chan_invalid: return "chan_invalid: Invalid channel object";
    case chan_invalidarg: return "chan_invalidarg: Invalid argument to function";
    case chan_timedout: return "chan_timedout: Operation timed out";
    case chan_closed: return "chan_closed: Channel is closed";
    case chan_nomem: return "chan_nomem: No memory available";    
    case chan_error: return "chan_error: System error";
    default: return "(unknown error)";
    }
}

#endif

#ifdef chan_unittest

#include <assert.h>

typedef chan_of(int, 32) int_chan_t;

bool chan_basictest() {
    puts("chan_basictest");

    int_chan_t ch;
    chan_init(&ch);
    printf("chan_send: %s\n", chan_errorstr( chan_send(&ch, 42) ));

    int result;
    printf("chan_receive: %s\n", chan_errorstr( chan_receive(&ch, &result) ));
    assert(result == 42);

    chan_destroy(&ch);

    return true;
}

bool chan_closedtest() {
    puts("chan_closedtest");

    int_chan_t ch;
    chan_init(&ch);
    printf("chan_send: %s\n", chan_errorstr( chan_send(&ch, 42) ));

    int result;
    printf("chan_receive: %s\n", chan_errorstr( chan_receive(&ch, &result) ));
    assert(result == 42);

    chan_close(&ch);

    assert(chan_receive(&ch, &result) == chan_closed);

    chan_destroy(&ch);

    return true;
}


static int producer_thread(void* arg) {
    int_chan_t* ch = arg;
    for (int i = 0; i < 100; i++) {
        printf("sending: %d\n", i);
        chan_send(ch, i);
        thrd_sleep(&(struct timespec){.tv_nsec=48*1000000}, NULL);
    }
    chan_close(ch);
    return 0;
}

bool chan_asynctest() {
    puts("chan_asynctest");

    thrd_t thread;
    int_chan_t ch;

    chan_init(&ch);

    thrd_create(&thread, producer_thread, &ch);

    int prevresult = -1;
    int result = 0;
    int err;
    while (!(err = chan_receive(&ch, &result))) {
        printf("received: %d\n", result);
        assert((result - prevresult) == 1);
        prevresult = result;
        thrd_sleep(&(struct timespec){.tv_nsec=200*1000000}, NULL);
    }
    assert(err == chan_closed);

    chan_destroy(&ch);
    thrd_join(thread, NULL);

    return true;
}

bool chan_runtest() {
    return chan_basictest() && chan_closedtest() && chan_asynctest();
}

#endif
