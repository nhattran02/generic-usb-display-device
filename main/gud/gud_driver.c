// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vladimir Avtsenov
// Based on driver.c from notro/gud-pico, Copyright (c) 2021-2024 Noralf Trønnes
/*
 * GUD (Generic USB Display) device side for TinyUSB on ESP32-S3 (LCDWiki ES3C28P port).
 * Derived from notro/gud-pico (driver.c), adapted to:
 *  - a composite device (vendor interface is not necessarily interface 0),
 *  - a pipeline: the USB task only receives rectangles, a render task on the other core decodes them and
 *    streams the pixels to disp->rect_* (SPI panel), with two receive buffers so the next rectangle comes
 *    in over USB while the current one is drawn,
 *  - a streaming LZ4 decoder working in a 64 KB internal-SRAM ring (gud_lz4.c).
 *
 * Flow control: when both receive buffers are taken (drawing is slower than USB), SET_BUFFER is accepted
 * but the bulk transfer is armed only once the render task frees a buffer; until then the controller NAKs
 * the host's bulk OUT packets, the standard USB way to say "not yet".
 */
#include <string.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "gud.h"
#include "gud_driver.h"
#include "gud_lz4.h"
#include "usb_glue.h"

static const char *TAG = "gud";

#define GUD_CTRL_REQ_BUF_SIZE   128 /* fits EDID (128 bytes) */
#define GUD_BULK_EP_SIZE        64
/*
 * ESP32-S3 OTG has a 7-bit DOEPTSIZ packet counter (OTG_PACKET_COUNT_WIDTH), and the DWC2 port
 * doesn't clamp it: an xfer over 127 packets wraps the counter and the endpoint stalls mid-transfer
 * (host sees -ETIMEDOUT). Keep each xfer within 127 max-size packets.
 */
#define GUD_EDPT_MAX_PACKETS    127
#define GUD_EDPT_XFER_MAX_SIZE  (GUD_EDPT_MAX_PACKETS * GUD_BULK_EP_SIZE)

#define min(a, b) (((a) < (b)) ? (a) : (b))

typedef struct {
    uint8_t  itf_num;
    uint8_t  ep_out;
    uint8_t  rhport;
    bool     opened;

    int8_t   rx_idx;     /* receive buffer the armed bulk transfer writes into, -1 = none */
    bool     pending;    /* SET_BUFFER accepted, bulk not armed yet (no free receive buffer) */
    uint8_t *buf;        /* where the bulk data lands */
    uint32_t xfer_len;   /* bytes on the wire (compressed_length or length) */
    uint32_t offset;
    int64_t  t_set;      /* SET_BUFFER received */
    int64_t  t_armed;    /* bulk transfer armed */
    struct gud_set_buffer_req req;
} gud_interface_t;

/* A received rectangle waiting for the render task */
typedef struct {
    struct gud_set_buffer_req req;
    uint8_t  idx;        /* receive buffer */
    uint32_t gen;        /* _gen when it was received */
} gud_job_t;

static gud_interface_t _gud = { .rx_idx = -1 };

static const struct gud_display *_display;
static uint8_t *_rx[GUD_RX_BUFS];
static size_t   _buf_size;
static uint8_t *_window;

static QueueHandle_t _job_q;    /* received rectangles, USB task -> render task */
static QueueHandle_t _free_q;   /* free receive buffer indices, render task -> USB task */
static volatile uint32_t _gen;  /* bumped by gud_driver_discard() */

static uint8_t _ctrl_req_buf[GUD_CTRL_REQ_BUF_SIZE] TU_ATTR_ALIGNED(4);
static uint8_t _status TU_ATTR_ALIGNED(4);

/* Updated by both tasks; the control transfer sends a consistent copy */
static struct gud_driver_stats _stats;
static struct gud_driver_stats _stats_tx TU_ATTR_ALIGNED(4);
static portMUX_TYPE _stats_lock = portMUX_INITIALIZER_UNLOCKED;

static void gud_render_task(void *arg);

esp_err_t gud_driver_setup(const struct gud_display *disp, void *const rx_bufs[GUD_RX_BUFS], size_t buf_size,
                           void *lz4_window, int render_core)
{
    _display = disp;
    _buf_size = buf_size;
    _window = lz4_window;

    _job_q = xQueueCreate(GUD_RX_BUFS, sizeof(gud_job_t));
    _free_q = xQueueCreate(GUD_RX_BUFS, sizeof(uint8_t));
    if (!_job_q || !_free_q)
        return ESP_ERR_NO_MEM;
    for (uint8_t i = 0; i < GUD_RX_BUFS; i++) {
        _rx[i] = rx_bufs[i];
        xQueueSend(_free_q, &i, 0);
    }
    if (xTaskCreatePinnedToCore(gud_render_task, "gud_render", 4096, NULL, 5, NULL, render_core) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void gud_driver_discard(void)
{
    _gen++;
}

/* ------------------------------------------------------------------------- */

static void gud_driver_init(void)
{
    memset(&_gud, 0, sizeof(_gud));
    _gud.rx_idx = -1;
}

static bool gud_driver_deinit(void)
{
    return true;
}

/* Hand an unused receive buffer back (USB task) */
static void gud_release_rx(void)
{
    if (_gud.rx_idx >= 0) {
        uint8_t idx = (uint8_t) _gud.rx_idx;
        _gud.rx_idx = -1;
        xQueueSend(_free_q, &idx, 0);
    }
}

static void gud_driver_reset(uint8_t rhport)
{
    (void) rhport;
    _gud.opened = false;
    _gud.pending = false;
    gud_release_rx();       /* the bus reset aborted the transfer */
    _status = 0;
}

static uint16_t gud_driver_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
{
    /* Only claim the vendor-class interface; HID etc. are handled by built-in drivers */
    TU_VERIFY(itf_desc->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC, 0);
    TU_VERIFY(itf_desc->bNumEndpoints == 1, 0);

    uint16_t const drv_len = sizeof(tusb_desc_interface_t) + sizeof(tusb_desc_endpoint_t);
    TU_VERIFY(max_len >= drv_len, 0);

    tusb_desc_endpoint_t const *desc_ep = (tusb_desc_endpoint_t const *) tu_desc_next(itf_desc);
    TU_ASSERT(usbd_edpt_open(rhport, desc_ep), 0);

    _gud.ep_out = desc_ep->bEndpointAddress;
    _gud.itf_num = itf_desc->bInterfaceNumber;
    _gud.rhport = rhport;
    _gud.opened = true;

    ESP_EARLY_LOGI(TAG, "GUD interface %u opened, EP OUT 0x%02x", _gud.itf_num, _gud.ep_out);
    return drv_len;
}

static bool gud_bulk_xfer(uint8_t rhport, uint8_t *buf, uint32_t xfer_len)
{
    TU_ASSERT(!usbd_edpt_busy(rhport, _gud.ep_out));

    if (buf) {
        TU_ASSERT(xfer_len);
        _gud.offset = 0;
        _gud.buf = buf;
        _gud.xfer_len = xfer_len;
    } else {
        _gud.offset += GUD_EDPT_XFER_MAX_SIZE;
        buf = _gud.buf + _gud.offset;
        xfer_len = _gud.xfer_len - _gud.offset;
    }

    if (xfer_len > GUD_EDPT_XFER_MAX_SIZE)
        xfer_len = GUD_EDPT_XFER_MAX_SIZE;

    return usbd_edpt_xfer(rhport, _gud.ep_out, buf, (uint16_t) xfer_len, false);
}

/* Arm the bulk transfer of the accepted SET_BUFFER into receive buffer idx (USB task) */
static bool gud_arm(uint8_t idx)
{
    const struct gud_set_buffer_req *r = &_gud.req;

    _gud.rx_idx = (int8_t) idx;
    _gud.t_armed = esp_timer_get_time();
    if (!gud_bulk_xfer(_gud.rhport, _rx[idx], r->compression ? r->compressed_length : r->length)) {
        gud_release_rx();
        return false;
    }
    taskENTER_CRITICAL(&_stats_lock);
    _stats.stall_us += (uint64_t) (_gud.t_armed - _gud.t_set);
    taskEXIT_CRITICAL(&_stats_lock);
    return true;
}

/* Deferred into the USB task by the render task after it freed a receive buffer */
static void gud_try_arm(void *param)
{
    (void) param;
    uint8_t idx;

    if (!_gud.opened || !_gud.pending || _gud.rx_idx >= 0)
        return;
    if (xQueueReceive(_free_q, &idx, 0) != pdTRUE)
        return;
    _gud.pending = false;
    if (!gud_arm(idx))
        ESP_LOGW(TAG, "can't arm bulk OUT");
}

/* Control SETUP stage (called from tud_task) */
static bool gud_control_request(uint8_t rhport, tusb_control_request_t const *req)
{
    uint16_t wLength = min(req->wLength, sizeof(_ctrl_req_buf));
    int ret;

    if (req->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE ||
        req->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR ||
        (uint8_t) req->wIndex != _gud.itf_num)
        return false;

    if (req->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (req->bRequest == GUD_REQ_GET_STATUS)
            return tud_control_xfer(rhport, req, &_status, sizeof(_status));

        _status = 0;
        ret = gud_req_get(_display, req->bRequest, req->wValue, _ctrl_req_buf, wLength);
        if (ret < 0) {
            _status = -ret;
            return false;
        }
        return tud_control_xfer(rhport, req, _ctrl_req_buf, ret);
    }

    /* OUT */
    _status = 0;
    if (!wLength) {
        ret = gud_req_set(_display, req->bRequest, req->wValue, _ctrl_req_buf, 0);
        if (ret < 0) {
            _status = -ret;
            return false;
        }
    }
    return tud_control_xfer(rhport, req, _ctrl_req_buf, wLength);
}

/* Control DATA stage complete (OUT requests with payload) */
static bool gud_control_complete(uint8_t rhport, tusb_control_request_t const *req)
{
    (void) rhport;
    if (req->bmRequestType_bit.direction != TUSB_DIR_OUT)
        return true;

    uint16_t wLength = min(req->wLength, sizeof(_ctrl_req_buf));
    int ret = gud_req_set(_display, req->bRequest, req->wValue, _ctrl_req_buf, wLength);
    if (ret < 0) {
        _status = -ret;
        return false;
    }

    if (req->bRequest == GUD_REQ_SET_BUFFER) {
        const struct gud_set_buffer_req *r = (const struct gud_set_buffer_req *) _ctrl_req_buf;

        if (gud_current_format() != GUD_PIXEL_FORMAT_RGB565 || r->length > _buf_size ||
            (r->compression && (r->compressed_length > _buf_size || !_window))) {
            _status = GUD_STATUS_INVALID_PARAMETER;
            return false;
        }
        /* The previous rectangle's bulk transfer is still armed (the host gave up on it) */
        if (_gud.rx_idx >= 0) {
            _status = GUD_STATUS_BUSY;
            return false;
        }

        _gud.req = *r;
        _gud.t_set = esp_timer_get_time();

        uint8_t idx;
        if (xQueueReceive(_free_q, &idx, 0) == pdTRUE) {
            _gud.pending = false;
            if (!gud_arm(idx)) {
                _status = GUD_STATUS_BUSY;
                return false;
            }
        } else {
            _gud.pending = true;        /* gud_try_arm() arms it once a buffer is free */
        }
    }
    return true;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req)
{
    /* Device-recipient vendor requests (outside GUD): reboot into the bootloader's USB download window,
     * performance counters for tools/gud_bench.c */
    if (req->bmRequestType_bit.recipient == TUSB_REQ_RCPT_DEVICE) {
        if (req->bRequest == USB_REQ_VENDOR_STATS && req->bmRequestType_bit.direction == TUSB_DIR_IN) {
            if (stage != CONTROL_STAGE_SETUP)
                return true;
            taskENTER_CRITICAL(&_stats_lock);
            _stats_tx = _stats;
            taskEXIT_CRITICAL(&_stats_lock);
            _stats_tx.version = GUD_DRIVER_STATS_VERSION;
            _stats_tx.uptime_us = (uint64_t) esp_timer_get_time();
            return tud_control_xfer(rhport, req, &_stats_tx, min(req->wLength, sizeof(_stats_tx)));
        }
        if (req->bRequest != USB_REQ_VENDOR_REBOOT || req->bmRequestType_bit.direction != TUSB_DIR_OUT)
            return false;
        if (stage == CONTROL_STAGE_SETUP)
            return tud_control_status(rhport, req);
        if (stage == CONTROL_STAGE_ACK)
            usb_glue_reboot();
        return true;
    }

    if (stage == CONTROL_STAGE_SETUP)
        return gud_control_request(rhport, req);
    if (stage == CONTROL_STAGE_DATA)
        return gud_control_complete(rhport, req);
    return true;
}

static bool gud_class_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req)
{
    (void) rhport; (void) stage; (void) req;
    return false;
}

/* Bulk OUT chunk received (USB task): re-arm for the next chunk, or hand the rectangle to the render task */
static bool gud_class_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void) ep_addr;

    if (result != XFER_RESULT_SUCCESS || _gud.rx_idx < 0) {
        gud_release_rx();
        return false;
    }

    if (xferred_bytes != (_gud.xfer_len - _gud.offset)) {
        if (xferred_bytes != GUD_EDPT_XFER_MAX_SIZE) {
            /* short transfer: host sent less than announced */
            ESP_LOGW(TAG, "short bulk: got %u, expected %u", (unsigned) xferred_bytes,
                     (unsigned) (_gud.xfer_len - _gud.offset));
            gud_release_rx();
            return false;
        }
        return gud_bulk_xfer(rhport, NULL, 0);
    }

    taskENTER_CRITICAL(&_stats_lock);
    _stats.usb_us += (uint64_t) (esp_timer_get_time() - _gud.t_armed);
    _stats.wire_bytes += _gud.xfer_len;
    taskEXIT_CRITICAL(&_stats_lock);

    gud_job_t job = { .req = _gud.req, .idx = (uint8_t) _gud.rx_idx, .gen = _gen };
    _gud.rx_idx = -1;
    xQueueSend(_job_q, &job, 0);    /* never full: one job per receive buffer */
    return true;
}

/* ------------------------------ render task ------------------------------- */

static void lz4_sink(void *ctx, const void *data, size_t len)
{
    (void) ctx;
    _display->rect_write(_display, data, len);
}

static void gud_render(const gud_job_t *job)
{
    const struct gud_set_buffer_req *r = &job->req;
    const uint8_t *buf = _rx[job->idx];
    uint32_t decoded;

    _display->rect_begin(_display, r);
    /* rect_begin() serialises with the firmware's own screens: checked after it, a discarded picture can't
     * slip in after "<NO SIGNAL>" */
    if (job->gen == _gen) {
        if (!r->compression)
            _display->rect_write(_display, buf, r->length);
        else if (!gud_lz4_decode(buf, r->compressed_length, r->length, _window, lz4_sink, NULL, &decoded))
            ESP_LOGW(TAG, "LZ4 failed: %u -> %u bytes, decoded %u", (unsigned) r->compressed_length,
                     (unsigned) r->length, (unsigned) decoded);
    }
    _display->rect_end(_display);
}

static void gud_render_task(void *arg)
{
    (void) arg;
    gud_job_t job;

    for (;;) {
        xQueueReceive(_job_q, &job, portMAX_DELAY);

        int64_t t0 = esp_timer_get_time();
        gud_render(&job);
        int64_t t1 = esp_timer_get_time();

        taskENTER_CRITICAL(&_stats_lock);
        _stats.rects++;
        _stats.pixels += (uint64_t) job.req.width * job.req.height;
        _stats.raw_bytes += job.req.length;
        _stats.render_us += (uint64_t) (t1 - t0);
        taskEXIT_CRITICAL(&_stats_lock);

        xQueueSend(_free_q, &job.idx, 0);
        usbd_defer_func(gud_try_arm, NULL, false);
    }
}

/* ------------------------------------------------------------------------- */

static usbd_class_driver_t const _gud_class_driver[] = {
    {
        .name            = "GUD",
        .init            = gud_driver_init,
        .deinit          = gud_driver_deinit,
        .reset           = gud_driver_reset,
        .open            = gud_driver_open,
        .control_xfer_cb = gud_class_control_xfer_cb,
        .xfer_cb         = gud_class_xfer_cb,
        .xfer_isr        = NULL,
        .sof             = NULL,
    },
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = TU_ARRAY_SIZE(_gud_class_driver);
    return _gud_class_driver;
}
