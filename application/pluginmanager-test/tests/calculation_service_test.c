/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "calculation_service.h"

struct fixture {
    const char *snapshot;
    int snapshot_status;
    int transform_status;
};

static int snapshot(void *context, char *destination, size_t capacity,
                    size_t *length)
{
    struct fixture *fixture = context;
    size_t size = strlen(fixture->snapshot);

    if (fixture->snapshot_status != 0) return fixture->snapshot_status;
    if (size > capacity) return -EFBIG;
    memcpy(destination, fixture->snapshot, size);
    *length = size;
    return 0;
}

static int transform(void *context, const char *input, size_t input_length,
                     int32_t offset, char *output, size_t capacity,
                     size_t *length)
{
    struct fixture *fixture = context;

    (void)offset;
    if (fixture->transform_status != 0) return fixture->transform_status;
    if (input_length + 1u > capacity) return -EFBIG;
    memcpy(output, input, input_length);
    output[input_length] = '\0';
    *length = input_length;
    return 0;
}

int main(void)
{
    struct fixture fixture = { .snapshot = "100\n-20\n" };
    struct ef800_calculation_service service = {
        .snapshot = snapshot,
        .transform = transform,
        .context = &fixture,
    };
    char snapshot_buffer[32];
    char output[32];
    size_t length = 0u;

    assert(ef800_calculation_service_run(&service, 5, snapshot_buffer,
                                         sizeof(snapshot_buffer), output,
                                         sizeof(output), &length) == 0);
    assert(length == 8u && memcmp(output, "100\n-20\n", length) == 0);

    fixture.snapshot_status = -EIO;
    assert(ef800_calculation_service_run(&service, 5, snapshot_buffer,
                                         sizeof(snapshot_buffer), output,
                                         sizeof(output), &length) == -EIO);
    fixture.snapshot_status = 0;
    assert(ef800_calculation_service_run(&service, 5, NULL,
                                         sizeof(snapshot_buffer), output,
                                         sizeof(output), &length) == -EINVAL);
    return 0;
}
