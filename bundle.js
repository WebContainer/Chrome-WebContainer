const {print, log} = wlibc

log("WebContainer::Init")

__GLOBAL__.window = {}

const {TextEncoder, TextDecoder} = require('text-encoding-shim')
// const hexdump = require('hexdump-js')
const syscalls = require('./syscalls.js')
const Utf8ArrayToStr = require('./src/Utf8ArrayToStr')

const memory = new WebAssembly.Memory({initial: 2})
const buffer = new Uint8Array(memory.buffer)

const STACK_BEGIN = 10000

// The stack/heap seaparator exists here.
// The stack grows downwards to zero.
// The heap grows upward to te page break.
buffer[1] = STACK_BEGIN

const PAGE_SIZE = 64 * 1042

// Allocate free space on the heap.
let malloc_offset = STACK_BEGIN + 1
function malloc(s) {
    const next = malloc_offset
    malloc_offset = malloc_offset + s
    return next
}

function nullTerminatedString(i) {
    let s = ""
    while(buffer[i] !== 0) {
        s += String.fromCharCode(buffer[i])
        i++
    }
    return s
}

const IOCTL = {
    TIOCGWINSZ: 0x5413
}

function ioctl_TIOCGWINSZ(structPtr) {
    // http://www.delorie.com/djgpp/doc/libc/libc_495.html
    
    const winsize = new Uint16Array(buffer.slice(structPtr, structPtr + 8).buffer)

    winsize[0] = 30 /* rows, in characters */
    winsize[1] = 60 /* columns, in characters */
    winsize[2] = 1000 /* horizontal size, pixels */
    winsize[3] = 1000 /* vertical size, pixels */

    return 0
}

function ioctl(fd, req, ...args) {
    switch(req) {
    case IOCTL.TIOCGWINSZ: {
        const [structPtr] = args
        ioctl_TIOCGWINSZ(structPtr)
    }; break
    default:
        return 0
    }
}

function brk(addrPtr) {
    // http://man7.org/linux/man-pages/man2/brk.2.html
    if (memory.buffer.byteLength < addrPtr) {
        const newPages = Math.ceil(addrPtr - memory.buffer.byteLength) / PAGE_SIZE
        memory.grow(newPages)
    }
    return 0
}

function clone(a, b) {
    // Just fail. This is hard.
    return -1
}

function rt_sigprocmask(how, set, oldset, sigsetsize) {
    // https://manpages.debian.org/testing/manpages-dev/rt_sigprocmask.2.en.html
    return 0 // LIES!
}

function writev(a, b, c) {
    const fd = a;
    const iovPtr = b;
    const iovCnt = c;
    const iovs = new Uint32Array(buffer.slice(iovPtr, iovPtr + 8 * iovCnt).buffer)
    let bytesWritten = 0
    for (let i = 0; i < iovCnt; i++) {
        const dataPtr = iovs[i*2+0]
        const byteCount = iovs[i*2+1]
        bytesWritten += byteCount
        const data = new Uint8Array(buffer.slice(dataPtr, dataPtr + byteCount).buffer)
        // print(s)
        wlibc.write(fd, data.buffer);
    }
    
    return bytesWritten
}

function openat(args) {
    const relfd = args[0] // "relative" fd to openat
    const filenamePtr = args[1]
    const filename = nullTerminatedString(filenamePtr)
    const flags = args[2]
    const mode = args[3]
    const fd = wlibc.open(filename)
    return fd
}

function read(args) {
    const [fd, ptr, count] = args
    const data = wlibc.read(fd, count)
    const bufa = new Uint8Array(data)
    
    for(let i=0; i < bufa.length; i++) {
        buffer[ptr + i] = bufa[i]
    }
    return bufa.length;
}

function write(args) {
    const [fd, strptr, len] = args
    return wlibc.write(fd, buffer.slice(strptr, strptr + len).buffer)
}

function close(args) {
    const [fd] = args
    return wlibc.close(fd)
}

// Allow dynamic turning on/off a tracer function
var tracer = false
function dlog(...args) {
    if (tracer) {
        log(args.join(' '))
    }
}

const imports = {
    env: {
        memory,
        print: (arg) => {
            let s = ""
            let i = arg
            while(buffer[i]) {
                s += String.fromCharCode(buffer[i])
                i++
            }
            wlibc.print(s)
        },
        __syscall: (syscallno, argsPointer) => {
            // grab VAR_ARGS off the stack
            const args = new Int32Array(buffer.slice(argsPointer, argsPointer + 4 * 7).buffer)
            const {name} = syscalls[syscallno]
            dlog('__syscall', name)
            switch (name) {
            case 'openat': {
                return openat(args)
            }; break;
            case 'read': {
                return read(args)
            }; break;
            case 'close': {
                return close(args)
            }; break;
            case 'write': {
                return write(args)
            }; break
            default:
                log(`Unknown __syscall: ${name} args ${args}`)
                return -1
            }
        },
        __syscall0: (syscallno) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall0', name)
            switch(name) {
            case 'gettid': {
                return 1
            }; break
            default:
                log(`Unknown __syscall0 ${name}`)
            }
        },
        __syscall1: (syscallno, a) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall1', name)
            switch(name) {
            case 'brk': {
                return brk(a)
            }; break
            default:
                log(`Unknown __syscall1 ${name}, ${a}`)
            }
        },
        __syscall2: (syscallno, a, b) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall2', name)
            switch(name) {
            case 'clone': {
                return clone(a, b)
            }; break
            default:
                log(`Unknown __syscall2 ${name}, ${a} ${b}`)
            }
        },
        __syscall7: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall7', name)
            switch(name) {
            default:    
                log(`Unknown __syscall7 ${name}, ${args}`)
            }
        },
        __syscall4: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall4', name)
            switch(name) {
            case 'rt_sigprocmask': {
                return rt_sigprocmask(...args)
            }; break
            default:
                log(`Unknown __syscall4 ${name}, ${args}`)
            }
        },
        __syscall5: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall5', name)
            switch(name) {
            default:    
                log(`Unknown __syscall5 ${name}, ${args}`)
            }
        },
        __syscall6: (syscallno, a, b, c, d, e, f) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall6', name)
            switch(name) {
            default:
                log(`Unknown __syscall6 ${name}, ${a} ${b} ${c} ${d} ${e} ${f}`)
            }
        },
        __syscall3: (syscallno, a, b, c) => {
            const {name} = syscalls[syscallno]
            dlog('__syscall3', name)
            switch (name) {
                case 'writev': {
                    return writev(a, b, c)
                }; break;
                case 'ioctl': {
                    return ioctl(a, b, c)
                }; break
                default: {
                    log(`Unknown __syscall3 ${name}, ${a} ${b} ${c}`)
                    return -1
                }
            }
        },

        _start() {},

        trace(onOff) {
            tracer = onOff
        }
    },
}

// Setup `main(int argc, char ** argv)`
// We have to write all the argv arguments to the head with our custom malloc.
// There is a lot of ArrayBuffer magic.
const argv = ["wasm", ...(__WASMARGS__.split(/\s+/))]
const argc = argv.length
const argvPointers = new Uint32Array(new ArrayBuffer(argv.length * 4))
var i = 0
for(const arg of argv) {
    const te = new TextEncoder()
    const be = te.encode(arg)
    const pt = malloc(be.byteLength + 1)
    
    // place each string on the heap
    buffer.set(be, pt)
    
    // record the pointer to another buffer
    // we will place this on the heap later
    argvPointers[i++] = pt
}
const st = malloc(argvPointers.byteLength)
// Need to recast our 32-bit pointer buffer to 8-bit typed array
// If we don't match the typed array sizes, weird stuff happens.
// If we cast them both to 32-bit arrays, the *st* pointer will be wrong.
buffer.set(new Uint8Array(argvPointers.buffer), st)


// Run WASM bundle
const o = WebAssembly
.instantiate(__WASMBUNDLE__, imports)
.then(r => {
    log("WebContainer::Begin")
    const exit = r.instance.exports.main(argc, st)
    log("WebContainer::Exit Code(" + exit + ")")
    return exit
})
.catch(e => {
    log(e.message)
    wlibc.exit(1)
})
.then(code => wlibc.exit(code||0))
