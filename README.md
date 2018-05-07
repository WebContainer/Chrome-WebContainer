# GOAL

1. WASM binary
2. create a libc that uses mojo to perform any privileged libc operations
    - e.g. `open()` or `socket()`

```
render                      node
  |                          |
 open                        |
  |   ------(mojo IPC)-----> |
  |                          |
(waiting)                  fs.open
  |                          |
  | <------(mojo IPC)------- |
  |                          |
```



# Notes

- https://chromium.googlesource.com/chromium/src/+/master/mojo/README.md
- https://chromium.googlesource.com/chromium/src/+/master/mojo/public/cpp/bindings/README.md
- https://chromium.googlesource.com/chromium/src/+/master/mojo/public/cpp/system/README.md
- https://www.chromium.org/developers/design-documents/mojo/synchronous-calls
- https://cs.chromium.org/chromium/src/mojo/public/cpp/system/wait.h
