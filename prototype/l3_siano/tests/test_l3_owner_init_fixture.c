/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Core 01c9951 integration fixture: a product adapter's initialize hook uses
 * the common owner transfer API while the owner is Opening, observes the
 * completion, and then lets the owner reach READY.
 */
#include <dtv/l3_owner.h>

#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct fixture {
    unsigned int submits;
    unsigned int completions;
    dtv_l3_context *owner;
};

static dtv_l3_claim_result fixture_claim(void *context,
                                         dtv_l3_provider_binding *binding,
                                         uint32_t claim_index)
{
    (void)context;
    (void)binding;
    (void)claim_index;
    return (dtv_l3_claim_result){DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
}

static dtv_l3_release_result fixture_release_claim(
    void *context, dtv_l3_provider_binding *binding, uint32_t claim_index)
{
    (void)context;
    (void)binding;
    (void)claim_index;
    return (dtv_l3_release_result){DTV_L3_RELEASE_QUIESCED,
                                   DTV_L3_CLAIM_NONE,
                                   DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
}

static dtv_l3_result fixture_submit(
    void *context, dtv_l3_provider_binding *binding, uint64_t transfer_id,
    const dtv_l3_transfer_request *request,
    dtv_l3_transfer_complete_fn complete, void *callback_context)
{
    struct fixture *fixture = context;
    dtv_l3_transfer_completion completion;
    (void)request;
    ++fixture->submits;
    memset(&completion, 0, sizeof(completion));
    completion.result = DTV_L3_OK;
    completion.actual_length = request->out_size;
    completion.actual_length_known = 1U;
    complete(binding, transfer_id, &completion, callback_context);
    ++fixture->completions;
    return DTV_L3_ACCEPTED;
}

static dtv_l3_result fixture_cancel(void *context,
                                    dtv_l3_provider_binding *binding,
                                    uint64_t transfer_id)
{
    (void)context;
    (void)binding;
    (void)transfer_id;
    return DTV_L3_OK;
}

static dtv_l3_result fixture_bind(void *context, dtv_l3_context *owner)
{
    struct fixture *fixture = context;
    fixture->owner = owner;
    return DTV_L3_OK;
}

static dtv_l3_result fixture_initialize(void *context, dtv_l3_context *owner)
{
    struct fixture *fixture = context;
    dtv_l3_transfer_request request;
    dtv_l3_op *op = NULL;
    dtv_l3_op_result result;
    dtv_l3_result status;
    uint8_t payload[8] = {0};
    unsigned int attempt;

    fixture->owner = owner;
    memset(&request, 0, sizeof(request));
    request.kind = DTV_L3_TRANSFER_BULK;
    request.endpoint = 0x02U;
    request.timeout_ms = 1000U;
    request.out_data = payload;
    request.out_size = sizeof(payload);
    request.claim_index = 0U;

    /* The owner is still Opening; Core 01c9951 must accept this transfer. */
    status = dtv_l3_transfer_start(owner, &request, &op);
    if (status != DTV_L3_ACCEPTED || op == NULL) {
        fprintf(stderr, "initialize transfer_start=%d\n", (int)status);
        return status == DTV_L3_ACCEPTED ? DTV_L3_FAILED : status;
    }
    status = dtv_l3_op_wait(op, 1000U);
    if (status != DTV_L3_OK) {
        fprintf(stderr, "initialize op_wait=%d\n", (int)status);
        (void)dtv_l3_op_release(op);
        return status;
    }
    for (attempt = 0; attempt < 100000U; ++attempt) {
        memset(&result, 0, sizeof(result));
        if (dtv_l3_op_poll(op, &result) != DTV_L3_OK) {
            (void)dtv_l3_op_release(op);
            return DTV_L3_FAILED;
        }
        if (result.state == DTV_L3_OP_DONE ||
            result.state == DTV_L3_OP_FAILED) {
            break;
        }
        sched_yield();
    }
    if (result.state != DTV_L3_OP_DONE || result.result != DTV_L3_OK) {
        fprintf(stderr, "initialize op state=%d result=%d\n", (int)result.state,
                (int)result.result);
        (void)dtv_l3_op_release(op);
        return DTV_L3_FAILED;
    }
    if (dtv_l3_op_release(op) != DTV_L3_OK) {
        return DTV_L3_FAILED;
    }
    if (fixture->submits != 1U || fixture->completions != 1U) {
        fprintf(stderr, "initialize submits=%u completions=%u\n",
                fixture->submits, fixture->completions);
        return DTV_L3_FAILED;
    }
    return DTV_L3_OK;
}

int main(void)
{
    struct fixture fixture;
    dtv_l3_usb_provider usb;
    dtv_l3_product_adapter product;
    dtv_l3_open_result opened;
    dtv_l3_open_status open_status;
    dtv_l3_close_status close_status;
    dtv_l3_result released = DTV_L3_BUSY;
    unsigned int attempt;
    int ok = 1;

    memset(&fixture, 0, sizeof(fixture));
    memset(&usb, 0, sizeof(usb));
    usb.provider_context = &fixture;
    usb.claim = fixture_claim;
    usb.release_claim = fixture_release_claim;
    usb.submit = fixture_submit;
    usb.cancel = fixture_cancel;
    usb.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;

    memset(&product, 0, sizeof(product));
    product.product_context = &fixture;
    product.bind = fixture_bind;
    product.initialize = fixture_initialize;

    opened = dtv_l3_open_ex(&usb, &product, 1U);
    if (opened.result != DTV_L3_ACCEPTED || opened.owner == NULL ||
        opened.op == NULL) {
        fprintf(stderr, "open_ex=%d\n", (int)opened.result);
        return 1;
    }
    for (attempt = 0; attempt < 1000000U; ++attempt) {
        dtv_l3_op_result result;
        if (dtv_l3_op_poll(opened.op, &result) != DTV_L3_OK) {
            return 1;
        }
        if (result.state == DTV_L3_OP_DONE) {
            if (result.result != DTV_L3_OK) {
                fprintf(stderr, "open result=%d\n", (int)result.result);
                return 1;
            }
            break;
        }
        if (result.state == DTV_L3_OP_FAILED) {
            fprintf(stderr, "open failed result=%d\n", (int)result.result);
            return 1;
        }
        sched_yield();
    }
    if (attempt == 1000000U) {
        return 1;
    }
    if (dtv_l3_op_release(opened.op) != DTV_L3_OK) {
        return 1;
    }
    memset(&open_status, 0, sizeof(open_status));
    if (dtv_l3_open_status_get(opened.owner, &open_status) != DTV_L3_OK ||
        open_status.state != DTV_L3_OPEN_READY ||
        fixture.submits != 1U || fixture.completions != 1U) {
        fprintf(stderr, "open status=%d submits=%u completions=%u\n",
                (int)open_status.state, fixture.submits, fixture.completions);
        return 1;
    }

    if (dtv_l3_close(opened.owner, 1000U) != DTV_L3_OK &&
        dtv_l3_close(opened.owner, 1000U) != DTV_L3_ACCEPTED) {
        return 1;
    }
    for (attempt = 0; attempt < 1000000U; ++attempt) {
        memset(&close_status, 0, sizeof(close_status));
        if (dtv_l3_close_status_get(opened.owner, &close_status) != DTV_L3_OK) {
            return 1;
        }
        if (close_status.cleanup_complete && close_status.all_ops_recovered) {
            break;
        }
        sched_yield();
    }
    if (attempt == 1000000U) {
        return 1;
    }
    for (attempt = 0; attempt < 1000000U && released == DTV_L3_BUSY;
         ++attempt) {
        released = dtv_l3_context_release(opened.owner);
        sched_yield();
    }
    if (released != DTV_L3_OK) {
        fprintf(stderr, "release=%d\n", (int)released);
        ok = 0;
    }
    if (ok != 0) {
        printf("core owner initialize transfer: PASS (submits=1, completions=1, state=READY)\n");
    }
    return ok != 0 ? 0 : 1;
}
