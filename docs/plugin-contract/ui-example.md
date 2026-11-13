# UI Example

An optional UI capability exposes bounded render and key callbacks:

```c
int32_t render(void *instance, struct ef800_plugin_screen *screen);
int32_t handle_key(void *instance,
    const struct ef800_plugin_key_event *event,
    enum ef800_plugin_key_result *result);
```

The plugin fills only body content. EF800Application renders the header and
footer itself. `EF800_PLUGIN_KEY_BACK` is reserved for host session teardown;
plugins receive logical character and directional events but never raw UART
frames.
