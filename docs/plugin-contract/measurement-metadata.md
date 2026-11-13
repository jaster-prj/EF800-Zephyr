# Measurement Metadata Extension

The generic PluginManager descriptor supports an opaque extension pointer and
size. PluginManager does not interpret this data.

EF800 Measurement plugins define the extension payload as:

```c
struct ef800_plugin_metadata {
    uint8_t metadata_id[16];
    uint16_t version;
    uint16_t size;
    uint32_t required_hardware;
};
```

`metadata_id` must equal `EF800_PLUGIN_METADATA_UUID_BYTES` and `version` must
equal `EF800_PLUGIN_METADATA_VERSION`.

Hardware bits are defined by `enum ef800_plugin_hardware_capability`:

- `EF800_PLUGIN_HARDWARE_SERIAL`
- `EF800_PLUGIN_HARDWARE_SERIAL_FLOW_CONTROL`
- `EF800_PLUGIN_HARDWARE_PARALLEL`
- `EF800_PLUGIN_HARDWARE_USB`
- `EF800_PLUGIN_HARDWARE_I2C`
- `EF800_PLUGIN_HARDWARE_SPI`

The extension is validated by EF800Application after package load. A malformed,
unknown-version, or unknown-UUID extension leaves the plugin unavailable for
hardware assignment.
