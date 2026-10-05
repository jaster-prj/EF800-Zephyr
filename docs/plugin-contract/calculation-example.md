# Calculation Example

A Calculation plugin exposes an API compatible with:

```c
struct ef800_calculation_api {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*transform)(void *instance,
        const char *snapshot,
        size_t snapshot_length,
        char *output,
        size_t output_capacity,
        size_t *written);
    uint32_t reserved[4];
};
```

The plugin receives a bounded immutable snapshot. It writes only to the host
provided output buffer and returns the exact output length. It must return an
error if the output does not fit.

The host previews results without mutation. A separate explicit confirmation
path validates and appends the result through MainApplication's Workspace API.
