const {print, log} = wlibc

log("WebContainer::Init")

__GLOBAL__.window = {}

const {TextEncoder, TextDecoder} = require('text-encoding-shim')
const {parseArgsStringToArgv} = require('string-argv')
// const hexdump = require('hexdump-js')
const syscalls = require('./syscalls.js')
const Utf8ArrayToStr = require('./src/Utf8ArrayToStr')

const memory = new WebAssembly.Memory({initial: 10})
const buffer = new Uint8Array(memory.buffer)

const STACK_BEGIN = 100000

// The stack/heap seaparator exists here.
// The stack grows downwards to zero.
// The heap grows upward to te page break.
// buffer[1] = STACK_BEGIN

const PAGE_SIZE = 64 * 1024

// Allocate free space on the heap.
let _malloc_offset = STACK_BEGIN + 1
function _malloc(s) {
    const next = _malloc_offset
    _malloc_offset = _malloc_offset + s
    return next
}

function nullTerminatedString(i) {
    const buffer = new Uint8Array(memory.buffer)
    let s = ""
    while(buffer[i] !== 0) {
        s += String.fromCharCode(buffer[i])
        i++
    }
    return s
}

const IOCTL_REQUESTS = {
    0x5401: "TCGETS",
    0x5402: "TCSETS",
    0x5403: "TCSETSW",
    0x5404: "TCSETSF",
    0x5405: "TCGETA",
    0x5406: "TCSETA",
    0x5407: "TCSETAW",
    0x5408: "TCSETAF",
    0x5409: "TCSBRK",
    0x540A: "TCXONC",
    0x540B: "TCFLSH",
    0x540C: "TIOCEXCL",
    0x540D: "TIOCNXCL",
    0x540E: "TIOCSCTTY",
    0x540F: "TIOCGPGRP",
    0x5410: "TIOCSPGRP",
    0x5411: "TIOCOUTQ",
    0x5412: "TIOCSTI",
    0x5413: "TIOCGWINSZ",
    0x5414: "TIOCSWINSZ",
    0x5415: "TIOCMGET",
    0x5416: "TIOCMBIS",
    0x5417: "TIOCMBIC",
    0x5418: "TIOCMSET",
    0x5419: "TIOCGSOFTCAR",
    0x541A: "TIOCSSOFTCAR",
    0x541B: "FIONREAD",
    FIONREAD: "TIOCINQ",
    0x541C: "TIOCLINUX",
    0x541D: "TIOCCONS",
    0x541E: "TIOCGSERIAL",
    0x541F: "TIOCSSERIAL",
    0x5420: "TIOCPKT",
    0x5421: "FIONBIO",
    0x5422: "TIOCNOTTY",
    0x5423: "TIOCSETD",
    0x5424: "TIOCGETD",
    0x5425: "TCSBRKP",
    0x5427: "TIOCSBRK",
    0x5428: "TIOCCBRK",
    0x5429: "TIOCGSID",
    0x542E: "TIOCGRS485",
    0x542F: "TIOCSRS485",
    0x80045430: "TIOCGPTN",
    0x40045431: "TIOCSPTLCK",
    0x80045432: "TIOCGDEV",
    0x5432: "TCGETX",
    0x5433: "TCSETX",
    0x5434: "TCSETXF",
    0x5435: "TCSETXW",
    0x40045436: "TIOCSIG",
    0x5437: "TIOCVHANGUP",
    0x80045438: "TIOCGPKT",
    0x80045439: "TIOCGPTLCK",
    0x80045440: "TIOCGEXCL",
    0x5441: "TIOCGPTPEER",
}

function ioctl_TIOCGWINSZ(structPtr) {
    dlog(`ioctl_TIOCGWINSZ`)
    // http://www.delorie.com/djgpp/doc/libc/libc_495.html
    const buffer = new Uint8Array(memory.buffer)
    const winsize = new Uint16Array(buffer.slice(structPtr, structPtr + 8).buffer)

    winsize[0] = 30 /* rows, in characters */
    winsize[1] = 60 /* columns, in characters */
    winsize[2] = 1000 /* horizontal size, pixels */
    winsize[3] = 1000 /* vertical size, pixels */

    return 0
}

function ioctl(fd, req, ...args) {
    dlog(`ioctl: fd:${fd}, req:${req}, args:${args}`)
    switch(IOCTL_REQUESTS[req]) {
    case 'TIOCGWINSZ': {
        const [structPtr] = args
        ioctl_TIOCGWINSZ(structPtr)
    }; break
    default:
        dlog(`unknown ioctl ${IOCTL_REQUESTS[req]}`)
        return 0
    }
}

function brk(addrPtr) {
    // http://man7.org/linux/man-pages/man2/brk.2.html
    if (memory.buffer.byteLength < addrPtr) {
        const newPages = Math.ceil(addrPtr - memory.buffer.byteLength) / PAGE_SIZE
        dlog(`brk allocate ${newPages} new pages`)
        memory.grow(newPages)
    }
    return 0 // success
}

function clone(a, b) {
    // Just fail. This is hard.
    return -1
}

function rt_sigprocmask(how, set, oldset, sigsetsize) {
    // https://manpages.debian.org/testing/manpages-dev/rt_sigprocmask.2.en.html
    return 0 // LIES!
}

function rt_sigaction() {
    return 0 // LIES!
}

function writev(fd, iovPtr, iovCnt) {
    const buffer = new Uint8Array(memory.buffer)
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

function readv(fd, iovPtr, iovCnt) {
    const buffer = new Uint8Array(memory.buffer)
    const iovs = new Uint32Array(buffer.slice(iovPtr, iovPtr + 8 * iovCnt).buffer)
    
    let bytesRead = 0
    for (let i = 0; i < iovCnt; i++) {
        const dataPtr = iovs[i*2+0]
        const byteCount = iovs[i*2+1]
        const data = new Uint8Array(wlibc.read(fd, byteCount))

        dlog(`readv fd:${fd} ptr:${dataPtr} count:${byteCount}`)
    
        for (let i = 0; i < data.byteLength; i++) {
            buffer[dataPtr + i] = data[i]
        }
        bytesRead += data.byteLength;
        if (bytesRead < data.byteLength)
            break;
    }
    return bytesRead
}

function openat(relfd, filenamePtr, flags, mode) {
    const filename = nullTerminatedString(filenamePtr)

    dlog(`openat filename:(${filename}) mode:(${mode}) flags:(${flags})`)
    const fd = wlibc.open(filename)
    dlog(`fd=${fd}`)
    
    return fd
}

function read(fd, ptr, count) {
    const buffer = new Uint8Array(memory.buffer)

    dlog(`read fd:${fd} ptr:${ptr} count:${count}`)

    const data = wlibc.read(fd, count)
    const bufa = new Uint8Array(data)
    
    for(let i=0; i < bufa.length; i++) {
        buffer[ptr + i] = bufa[i]
    }
    return bufa.length;
}

function write(fd, strptr, len) {
    const buffer = new Uint8Array(memory.buffer)
    return wlibc.write(fd, buffer.slice(strptr, strptr + len).buffer)
}

function close(fd) {
    return wlibc.close(fd)
}

function clock_gettime(a, b) {
    dlog(`clock_gettime: ${a}, ${b}`)
    const now = Date.now()
    const sec = parseInt(now/1000)
    const nan = now % 1000
    const buffer = new Uint32Array(memory.buffer.slice(b))
    buffer[0] = sec
    buffer[1] = nan * 1e6
    return 0
}

function invert_dictionary(obj) {
    return Object.entries(obj).reduce((m, [k, v]) => ({...m, [v]: k}), {})
}

const mmap_flags = {
    0: 'MAP_FILE',
    0x01: 'MAP_SHARED',
    0x02: 'MAP_PRIVATE',
    0x03: 'MAP_SHARED_VALIDATE',
    0x0f: 'MAP_TYPE',
    0x10: 'MAP_FIXED',
    0x20: 'MAP_ANON',
    0x4000: 'MAP_NORESERVE',
    0x0100: 'MAP_GROWSDOWN',
    0x0800: 'MAP_DENYWRITE',
    0x1000: 'MAP_EXECUTABLE',
    0x2000: 'MAP_LOCKED',
    0x8000: 'MAP_POPULATE',
    0x10000: 'MAP_NONBLOCK',
    0x20000: 'MAP_STACK',
    0x40000: 'MAP_HUGETLB',
    0x80000: 'MAP_SYNC',
    26: 'MAP_HUGE_SHIFT',
    0x3f: 'MAP_HUGE_MASK',
    [(16 << 26)]: 'MAP_HUGE_64KB',
    [(19 << 26)]: 'MAP_HUGE_512KB',
    [(20 << 26)]: 'MAP_HUGE_1MB',
    [(21 << 26)]: 'MAP_HUGE_2MB',
    [(23 << 26)]: 'MAP_HUGE_8MB',
    [(24 << 26)]: 'MAP_HUGE_16MB',
    [(28 << 26)]: 'MAP_HUGE_256MB',
    [(30 << 26)]: 'MAP_HUGE_1GB',
    [(31 << 26)]: 'MAP_HUGE_2GB',
    [(34 << 26)]: 'MAP_HUGE_16GB',  
}
const mmap_flags_reverse = invert_dictionary(mmap_flags)
const mmap_prot = {
    PROT_NONE: (1<<0),
    PROT_READ: (1<<1),
    PROT_WRITE: (1<<2),
    PROT_EXEC: (1<<3),
}
function mmap(ptr_addr, size_t_len, int_prot, int_flags, int_fd, off_t_offset) {
    dlog(`ptr_addr: ${ptr_addr}, size_t_len: ${size_t_len}, int_prot: ${int_prot}, int_flags:${int_flags}, int_fd: ${int_fd}, off_t_offset: ${off_t_offset}`)
    
    const readable = (int_prot & mmap_prot.PROT_READ) === mmap_prot.PROT_READ
    const writeable = (int_prot & mmap_prot.PROT_WRITE) === mmap_prot.PROT_WRITE
    const executable = (int_prot & mmap_prot.PROT_EXEC) === mmap_prot.PROT_EXEC
    dlog(`prot: ${readable ? 'r' : '-'}${writeable ? 'w' : '-'}${executable ? 'x' : '-'}`)

    Object.entries(mmap_flags_reverse).forEach(([flag, bits]) => {
        if ((int_flags & bits) === bits) {
            dlog(`mmap flag: ${flag}`)
        }
    })

    if (ptr_addr == 0 && int_fd == -1) {
        const new_pages = Math.ceil(65536/PAGE_SIZE)
        dlog(`mmap allocate ${new_pages} new pages`)
        const old_size = memory.grow(new_pages)
        dlog(`old number of pages ${old_size}`)
        const new_start = old_size * PAGE_SIZE
        dlog(`new page start ${new_start}`)
        return new_start
    } else {
        throw new Error(`NOT YET IMPLEMENTED`)
    }
}

// Allow dynamic turning on/off a tracer function
var tracer = __DEBUG__ == 'trace'
function dlog(...args) {
    if (tracer) {
        log(args.join(' '))
    }
}

const syscall_fns = {
    brk,
    clock_gettime,
    clone,
    close,
    ioctl,
    mmap,
    openat,
    read,
    readv,
    rt_sigaction,
    rt_sigprocmask,
    write,
    writev,
}

function syscall(syscallno, ...args) {
    const {name} = syscalls[syscallno]
    dlog('__syscall', name)
    const fn = syscall_fns[name]
    if (fn) {
        return fn(...args)
    } else {
        dlog(`Unknown syscall: ${name} args ${args}`)
        dlog(`${new Error().stack}`)
        return -1
    }
}

const imports = {
    env: {
        memory,
        print: (arg) => {
            const buffer = new Uint8Array(memory.buffer)
            let s = ""
            let i = arg
            while(buffer[i]) {
                s += String.fromCharCode(buffer[i])
                i++
            }
            wlibc.print(s)
        },
        __syscall: (syscallno, argsPointer) => {
            const buffer = new Uint8Array(memory.buffer)
            // grab VAR_ARGS off the stack
            const args = new Int32Array(buffer.slice(argsPointer, argsPointer + 4 * 7).buffer)
            syscall(syscallno, ...args)
        },
        __syscall0: syscall,
        __syscall1: syscall,
        __syscall2: syscall,
        __syscall3: syscall,
        __syscall4: syscall,
        __syscall5: syscall,
        __syscall6: syscall,
        __syscall7: syscall,
        __cxa_allocate_exception: (size) => {
            dlog(`__cxa_allocate_exception: ${size}`)
        },
        __cxa_throw: (exception, tinfo, dest) => {
            dlog(`__cxa_throw: ${exception}, ${tinfo}, ${dest}`)
        },

        _start() {},

        trace(onOff) {
            tracer = onOff
        },

        __cxa_allocate_exception() {
            return 0;
        },

        __cxa_throw() {
            throw new Error()
        }
    },
}

// Run WASM bundle
const o = WebAssembly
.instantiate(__WASMBUNDLE__, imports)
.then(r => {
    const { malloc = _malloc } = r.instance.exports

    // Setup `main(int argc, char ** argv)`
    // We have to write all the argv arguments to the head with our custom malloc.
    // There is a lot of ArrayBuffer magic.
    const argv = [__WASMBUNDLE_NAME__, ...parseArgsStringToArgv(__WASMARGS__)]
    const argc = argv.length
    const argvPointers = new Uint32Array(new ArrayBuffer(argv.length * 4))
    
    dlog(`argc: ${argc}`)
    dlog(`argv: (${argv.join('), (')})`)
    
    var i = 0
    for(const arg of argv) {
        const te = new TextEncoder()
        const be = te.encode(arg)
        const pt = malloc(be.byteLength + 1)
        
        // place each string on the heap
        new Uint8Array(memory.buffer).set(be, pt)
        
        // record the pointer to another buffer
        // we will place this on the heap later
        argvPointers[i++] = pt
    }
    const st = malloc(argvPointers.byteLength)
    // Need to recast our 32-bit pointer buffer to 8-bit typed array
    // If we don't match the typed array sizes, weird stuff happens.
    // If we cast them both to 32-bit arrays, the *st* pointer will be wrong.
    new Uint8Array(memory.buffer).set(new Uint8Array(argvPointers.buffer), st)
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
