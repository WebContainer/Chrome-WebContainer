# WebContainers

A web container is a brokered POSIX implementation for WebAssembly.
Applications running in web containers have mediated access to the underlying system.
We use Chromium with the intent to fully sandbox any running web containers.
We use Chromium's cross-platform IPC system [mojo](https://chromium.googlesource.com/chromium/src/+/master/mojo/README.md) to mediate access between the running WebAssembly and underlying operating system.

The current proof-of-concept barely works. Don't try it.

# Setup

You _need_ a Chrome checkout for this to work.
I am sure some kind soul will explain that part.

```
cd $CHROME_SRC
git clone https://github.com/groundwater/Chrome-WebContainers.git webcontainer
gn gen out/Default  --args='root_extra_deps=["//webcontainer"]'
ninja -C out/Default webcontainer
./out/Default/webcontainerd
```

# Implementation

Given a WASM binary. E.g. see [WASM C Example](https://github.com/groundwater/wasm-c-example). We create a libc-compatible system which that _wasm_ binary can interact with e.g. `open()` or `socket()`.

Those _system calls_ requiring I/O will be brokered through a privileged process.

```
(sandbox)                 (privileged)
webcontainer                 broker
    |                          |
   open                        |
    |   ------(mojo IPC)-----> |
    |                          |
  (waiting)                 libc.open
    |                          |
    | <------(mojo IPC)------- |
    |                          |
```

# Status

- [x] Compatible WASM toolchain in LLVM
  - See [WASM C Example](https://github.com/groundwater/wasm-c-example)
- [x] Mojo IPC Scaffolding to privileged process
- [ ] Working Examples
  - [x] `open`/`read`/`close` currently in `test.wasm`
  - [ ] `socket`/`listen`/`accept`
- [ ] Sandboxed `webcontainerc` process
- [ ] Security policy in `webcontainerd` broker
  - [ ] filesystem restrictions
  - [ ] network restrictions

# Notes

- https://chromium.googlesource.com/chromium/src/+/master/mojo/README.md
- https://chromium.googlesource.com/chromium/src/+/master/mojo/public/cpp/bindings/README.md
- https://chromium.googlesource.com/chromium/src/+/master/mojo/public/cpp/system/README.md
- https://www.chromium.org/developers/design-documents/mojo/synchronous-calls
- https://cs.chromium.org/chromium/src/mojo/public/cpp/system/wait.h
