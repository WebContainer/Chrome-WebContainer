# WebContainers

A web container is a brokered POSIX implementation in WebAssembly.
Applications running in web containers have mediated access to the underlying system.

The current proof-of-concept barely works. Don't try it.

# Setup

You _need_ a Chrome checkout for this to work.
I am sure some kind soul will explain that part.

```
cd $CHROME_SRC
git clone https://github.com/groundwater/Chrome-WebContainers.git webcontainer
gn build out/Default
ninja -C out/Default webcontainerd
npm run build
./out/Default/webcontainerd
```

# GOAL

1. WASM binary. See [WASM C Example](https://github.com/groundwater/wasm-c-example).
2. create a libc that uses mojo to perform any privileged libc operations
    - e.g. `open()` or `socket()`

```
(sandbox)                 (privileged)
  render                      node
    |                          |
   open                        |
    |   ------(mojo IPC)-----> |
    |                          |
  (waiting)                 libc.open
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
