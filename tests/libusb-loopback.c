/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <libusb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "protocol.h"

#define LOOPBACK_TRANSFERS 64U

struct libusb_device { int unused; };
struct libusb_device_handle { struct libusb_device *device; };

static struct libusb_device fake_device;
static struct libusb_device_handle fake_handle = {&fake_device};
static struct libusb_endpoint_descriptor fake_endpoints[2];
static struct libusb_interface_descriptor fake_altsetting;
static struct libusb_interface fake_interface;
static struct libusb_config_descriptor fake_config;
static bool suppress_tune_response;
static uint32_t last_tune_hz;
static unsigned tune_requests;
static unsigned pid_requests;

static pthread_mutex_t loop_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t loop_changed = PTHREAD_COND_INITIALIZER;
static struct libusb_transfer *transfers[LOOPBACK_TRANSFERS];
static bool pending[LOOPBACK_TRANSFERS];
static struct libusb_transfer *events[LOOPBACK_TRANSFERS * 2U];
static size_t event_head;
static size_t event_count;
static unsigned control_requests;
static uint16_t last_control_type;
static int next_bulk_error;
int test_libusb_deliver(struct libusb_transfer*, const uint8_t*, size_t);
int test_libusb_pending(struct libusb_transfer*);
static int queue_event(struct libusb_transfer *transfer);

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *context,
                                           libusb_device ***list)
{
    (void)context;
    *list = calloc(2U, sizeof(**list));
    if (!*list) return LIBUSB_ERROR_NO_MEM;
    (*list)[0] = &fake_device;
    return 1;
}

void LIBUSB_CALL libusb_free_device_list(libusb_device **list, int unref_devices)
{
    (void)unref_devices;
    free(list);
}

uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device *device)
{ (void)device; return 1U; }
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device *device)
{ (void)device; return 1U; }

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *device,
                                              struct libusb_device_descriptor *descriptor)
{
    (void)device;
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->idVendor = 0x3275U;
    descriptor->idProduct = 0x0080U;
    return 0;
}

int LIBUSB_CALL libusb_open(libusb_device *device, libusb_device_handle **handle)
{
    (void)device;
    *handle = &fake_handle;
    return 0;
}
libusb_device * LIBUSB_CALL libusb_get_device(libusb_device_handle *handle)
{ return handle->device; }

int LIBUSB_CALL libusb_get_active_config_descriptor(libusb_device *device,
                                                     struct libusb_config_descriptor **config)
{
    (void)device;
    memset(fake_endpoints, 0, sizeof(fake_endpoints));
    fake_endpoints[0].bEndpointAddress = 0x81U;
    fake_endpoints[0].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
    fake_endpoints[0].wMaxPacketSize = 512U;
    fake_endpoints[1].bEndpointAddress = 0x02U;
    fake_endpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
    fake_endpoints[1].wMaxPacketSize = 512U;
    memset(&fake_altsetting, 0, sizeof(fake_altsetting));
    fake_altsetting.bInterfaceNumber = 0U;
    fake_altsetting.bNumEndpoints = 2U;
    fake_altsetting.endpoint = fake_endpoints;
    memset(&fake_interface, 0, sizeof(fake_interface));
    fake_interface.altsetting = &fake_altsetting;
    fake_interface.num_altsetting = 1;
    memset(&fake_config, 0, sizeof(fake_config));
    fake_config.bNumInterfaces = 1U;
    fake_config.interface = &fake_interface;
    *config = &fake_config;
    return 0;
}
void LIBUSB_CALL libusb_free_config_descriptor(struct libusb_config_descriptor *config)
{ (void)config; }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *handle, int interface_number)
{ (void)handle; (void)interface_number; return LIBUSB_ERROR_NOT_SUPPORTED; }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *handle, int interface_number)
{ (void)handle; (void)interface_number; return 0; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *handle, int interface_number)
{ (void)handle; (void)interface_number; return 0; }
int LIBUSB_CALL libusb_clear_halt(libusb_device_handle *handle, unsigned char endpoint)
{ (void)handle; (void)endpoint; return 0; }
void LIBUSB_CALL libusb_close(libusb_device_handle *handle) { (void)handle; }

static int deliver_response(uint16_t type, const uint8_t *payload, size_t payload_length)
{
    uint8_t message[SMS_HEADER_SIZE + 16U];
    size_t length = SMS_HEADER_SIZE + payload_length;
    sms_pack_header(message, type, 0U, 0U, (uint16_t)length, 0U);
    if (payload_length) memcpy(message + SMS_HEADER_SIZE, payload, payload_length);
    for (size_t i = 0U; i < LOOPBACK_TRANSFERS; ++i) {
        pthread_mutex_lock(&loop_mutex);
        struct libusb_transfer *transfer = transfers[i];
        if (transfer && pending[i]) {
            pending[i] = false;
            memcpy(transfer->buffer, message, length);
            transfer->status = LIBUSB_TRANSFER_COMPLETED;
            transfer->actual_length = (int)length;
            const int rc = queue_event(transfer);
            pthread_mutex_unlock(&loop_mutex);
            if (rc != 0) return rc;
            for (unsigned attempt = 0U; attempt < 1000U; ++attempt) {
                if (test_libusb_pending(transfer)) return 0;
                struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000L};
                nanosleep(&pause, NULL);
            }
            return LIBUSB_ERROR_TIMEOUT;
        }
        pthread_mutex_unlock(&loop_mutex);
    }
    return LIBUSB_ERROR_NOT_FOUND;
}

void test_libusb_suppress_tune_response(bool suppress)
{ suppress_tune_response = suppress; }
void test_libusb_fail_next_bulk(int error)
{ next_bulk_error = error; }
int test_libusb_fail_stream(int status)
{
    pthread_mutex_lock(&loop_mutex);
    for (size_t i = 0U; i < LOOPBACK_TRANSFERS; ++i) {
        if (transfers[i] && pending[i]) {
            pending[i] = false;
            transfers[i]->status = status;
            transfers[i]->actual_length = 0;
            const int rc = queue_event(transfers[i]);
            pthread_mutex_unlock(&loop_mutex);
            return rc;
        }
    }
    pthread_mutex_unlock(&loop_mutex);
    return LIBUSB_ERROR_NOT_FOUND;
}
int test_libusb_deliver_control_to(struct libusb_transfer *transfer, uint16_t type)
{
    uint8_t message[SMS_HEADER_SIZE];
    sms_pack_header(message, type, 0U, 0U, sizeof(message), 0U);
    return test_libusb_deliver(transfer, message, sizeof(message));
}
uint32_t test_libusb_last_tune_hz(void) { return last_tune_hz; }
unsigned test_libusb_tune_requests(void) { return tune_requests; }
unsigned test_libusb_pid_requests(void) { return pid_requests; }
unsigned test_libusb_control_requests(void);
uint16_t test_libusb_last_control_type(void);

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *handle,
                                      unsigned char endpoint, unsigned char *data,
                                      int length, int *transferred, unsigned int timeout)
{
    (void)handle; (void)timeout;
    if ((endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT && length >= (int)SMS_HEADER_SIZE) {
        if (next_bulk_error != 0) {
            const int error = next_bulk_error;
            next_bulk_error = 0;
            *transferred = 0;
            return error;
        }
        const uint16_t type = sms_get_le16(data);
        ++control_requests;
        last_control_type = type;
        *transferred = length;
        if (type == MSG_SMS_GET_VERSION_EX_REQ) {
            uint8_t payload[12] = {0};
            payload[4] = DEVICE_MODE_ISDBT_BDA;
            payload[5] = (uint8_t)(1U << DEVICE_MODE_ISDBT_BDA);
            payload[10] = 8U;
            return deliver_response(MSG_SMS_GET_VERSION_EX_RES, payload, sizeof(payload));
        }
        if (type == MSG_SMS_ISDBT_TUNE_REQ) {
            ++tune_requests;
            last_tune_hz = sms_get_le32(data + SMS_HEADER_SIZE);
            if (suppress_tune_response) return 0;
            int rc = deliver_response(MSG_SMS_ISDBT_TUNE_RES, NULL, 0U);
            if (rc != 0) return rc;
            return deliver_response(MSG_SMS_SIGNAL_DETECTED_IND, NULL, 0U);
        }
        uint16_t response_type = type == MSG_SMS_ADD_PID_FILTER_REQ
            ? MSG_SMS_ADD_PID_FILTER_RES : 0U;
        if (type == MSG_SMS_ADD_PID_FILTER_REQ) ++pid_requests;
        if (response_type) return deliver_response(response_type, NULL, 0U);
        return 0;
    }
    *transferred = length;
    return 0;
}

unsigned test_libusb_control_requests(void) { return control_requests; }
uint16_t test_libusb_last_control_type(void) { return last_control_type; }

static size_t transfer_slot(struct libusb_transfer *transfer)
{
    for (size_t i = 0U; i < LOOPBACK_TRANSFERS; ++i) {
        if (transfers[i] == transfer) return i;
    }
    return LOOPBACK_TRANSFERS;
}

static int queue_event(struct libusb_transfer *transfer)
{
    if (event_count == LOOPBACK_TRANSFERS * 2U) return LIBUSB_ERROR_BUSY;
    events[(event_head + event_count) % (LOOPBACK_TRANSFERS * 2U)] = transfer;
    ++event_count;
    pthread_cond_broadcast(&loop_changed);
    return 0;
}

struct libusb_transfer * LIBUSB_CALL libusb_alloc_transfer(int iso_packets)
{
    (void)iso_packets;
    struct libusb_transfer *transfer = calloc(1U, sizeof(*transfer));
    if (!transfer) return NULL;
    pthread_mutex_lock(&loop_mutex);
    size_t slot = transfer_slot(NULL);
    if (slot == LOOPBACK_TRANSFERS) {
        pthread_mutex_unlock(&loop_mutex);
        free(transfer);
        return NULL;
    }
    transfers[slot] = transfer;
    pthread_mutex_unlock(&loop_mutex);
    return transfer;
}

void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *transfer)
{
    pthread_mutex_lock(&loop_mutex);
    size_t slot = transfer_slot(transfer);
    if (slot != LOOPBACK_TRANSFERS) {
        transfers[slot] = NULL;
        pending[slot] = false;
    }
    pthread_mutex_unlock(&loop_mutex);
    free(transfer);
}

int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *transfer)
{
    pthread_mutex_lock(&loop_mutex);
    size_t slot = transfer_slot(transfer);
    if (slot == LOOPBACK_TRANSFERS || pending[slot]) {
        pthread_mutex_unlock(&loop_mutex);
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    pending[slot] = true;
    pthread_mutex_unlock(&loop_mutex);
    return 0;
}

int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *transfer)
{
    pthread_mutex_lock(&loop_mutex);
    size_t slot = transfer_slot(transfer);
    if (slot == LOOPBACK_TRANSFERS || !pending[slot]) {
        pthread_mutex_unlock(&loop_mutex);
        return LIBUSB_ERROR_NOT_FOUND;
    }
    pending[slot] = false;
    transfer->status = LIBUSB_TRANSFER_CANCELLED;
    transfer->actual_length = 0;
    int rc = queue_event(transfer);
    pthread_mutex_unlock(&loop_mutex);
    return rc;
}

int LIBUSB_CALL libusb_handle_events_timeout(libusb_context *context,
                                              struct timeval *timeout)
{
    (void)context;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout->tv_sec;
    deadline.tv_nsec += timeout->tv_usec * 1000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&loop_mutex);
    while (event_count == 0U)
        if (pthread_cond_timedwait(&loop_changed, &loop_mutex, &deadline) != 0) break;
    if (event_count == 0U) {
        pthread_mutex_unlock(&loop_mutex);
        return 0;
    }
    struct libusb_transfer *transfer = events[event_head];
    event_head = (event_head + 1U) % (LOOPBACK_TRANSFERS * 2U);
    --event_count;
    pthread_mutex_unlock(&loop_mutex);
    transfer->callback(transfer);
    return 0;
}

int test_libusb_deliver(struct libusb_transfer *transfer, const uint8_t *bytes,
                        size_t length)
{
    if (!transfer || !bytes || length > (size_t)transfer->length) return -1;
    pthread_mutex_lock(&loop_mutex);
    size_t slot = transfer_slot(transfer);
    if (slot == LOOPBACK_TRANSFERS || !pending[slot]) {
        pthread_mutex_unlock(&loop_mutex);
        return -1;
    }
    pending[slot] = false;
    memcpy(transfer->buffer, bytes, length);
    transfer->status = LIBUSB_TRANSFER_COMPLETED;
    transfer->actual_length = (int)length;
    int rc = queue_event(transfer);
    pthread_mutex_unlock(&loop_mutex);
    return rc;
}

int test_libusb_pending(struct libusb_transfer *transfer)
{
    pthread_mutex_lock(&loop_mutex);
    size_t slot = transfer_slot(transfer);
    int result = slot != LOOPBACK_TRANSFERS && pending[slot];
    pthread_mutex_unlock(&loop_mutex);
    return result;
}
