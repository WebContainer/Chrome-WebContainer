const {print, log, open, read} = wlibc

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
            const args = new Int32Array(buffer.slice(argsPointer, argsPointer + 4 * 7).buffer)
            const {name} = syscalls[syscallno]
            switch (name) {
                case 'openat': {
                    const relfd = args[0] // "relative" fd to openat
                    const filenamePtr = args[1]
                    const filename = nullTerminatedString(filenamePtr)
                    const flags = args[2]
                    const mode = args[3]
                    const fd = wlibc.open(filename)
                    return fd
                }; break;
                case 'read': {
                    const [fd, ptr, count] = args
                    const data = wlibc.read(fd, count)
                    const bufa = new Uint8Array(data)
                    
                    for(let i=0; i < bufa.length; i++) {
                        buffer[ptr + i] = bufa[i]
                    }
                    return bufa.length;
                }; break;
                case 'close': {
                    const [fd] = args
                    return wlibc.close(fd)
                }; break;
                case 'write': {
                    const [fd, strptr, len] = args
                    return wlibc.write(fd, buffer.slice(strptr, strptr + len).buffer)
                }; break
                default: {
                    print(`__syscall: ${name} args ${args}`)
                    return -1
                }; break;
            }
        },
        __syscall1: (syscallno, a) => {
            const {name} = syscalls[syscallno]
            switch(name) {
            case 'brk': {
                return brk(a)
            }; break
            }
            log(`Syscall1 ${name}, ${a}`)
        },
        __syscall2: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            log(`Syscall2 ${name}, ${args}`)
        },
        __syscall7: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            log(`Syscall7 ${name}, ${args}`)
        },
        __syscall4: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            log(`Syscall4 ${name}, ${args}`)
        },
        __syscall5: (syscallno, ...args) => {
            const {name} = syscalls[syscallno]
            log(`Syscall5 ${name}, ${args}`)
        },
        __syscall6: (syscallno, a, b, c, d, e, f) => {
            const {name} = syscalls[syscallno]
            switch(name) {
            default:
                log(`Syscall6 ${name}, ${a} ${b} ${c} ${d} ${e} ${f}`)
            }
        },
        __syscall3: (syscallno, a, b, c) => {
            const {name} = syscalls[syscallno]
            switch (name) {
                case 'writev': {
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
                }; break;
                case 'ioctl': {
                    return ioctl(a, b, c)
                }; break
                default: {
                    log(`Syscall3 ${name}, ${a} ${b} ${c}`)
                    return -1
                }; break;
            }
        },
        // See: https://gcc.gnu.org/onlinedocs/gcc-3.4.0/gccint/Soft-float-library-routines.html
        __addsf3: (a, b) => a + b,
        __adddf3: (a, b) => a + b,
        __addtf3: (a, b) => a + b,
        __addxf3: (a, b) => a + b,

        __subsf3: (a, b) => a - b,
        __subdf3: (a, b) => a - b,
        __subtf3: (a, b) => a - b,
        __subxf3: (a, b) => a - b,

        __mulsf3: (a, b) => a * b,
        __muldf3: (a, b) => a * b,
        __multf3: (a, b) => a * b,
        __mulxf3: (a, b) => a * b,

        __divsf3: (a, b) => parseInt(a/b),
        __divdf3: (a, b) => parseInt(a/b),
        __divtf3: (a, b) => parseInt(a/b),
        __divxf3: (a, b) => parseInt(a/b),

        __negsf2: a => -a,
        __negdf2: a => -a,
        __negtf2: a => -a,
        __negxf2: a => -a,

        __extendsfdf2: a => a,
        __extenddftf2: a => a,
        __extendsfxf2: a => a,
        __extenddftf2: a => a,
        __extenddfxf2: a => a,

        __truncxfdf2: a => a,
        __trunctfdf2: a => a,
        __truncxfsf2: a => a,
        __trunctfsf2: a => a,
        __truncdfsf2: a => a,

        __fixsfsi: a => parseInt(a),
        __fixdfsi: a => parseInt(a),
        __fixtfsi: a => parseInt(a),
        __fixxfsi: a => parseInt(a),

        __fixsfdi: a => parseInt(a),
        __fixdfdi: a => parseInt(a),
        __fixtfdi: a => parseInt(a),
        __fixxfdi: a => parseInt(a),

        __fixunssfsi: a => a > 0 ? parseInt(a) : 0,
        __fixunsdfsi: a => a > 0 ? parseInt(a) : 0,
        __fixunstfsi: a => a > 0 ? parseInt(a) : 0,
        __fixunsxfsi: a => a > 0 ? parseInt(a) : 0,

        __fixunssfdi: a => a > 0 ? parseInt(a) : 0,
        __fixunsdfdi: a => a > 0 ? parseInt(a) : 0,
        __fixunstfdi: a => a > 0 ? parseInt(a) : 0,
        __fixunsxfdi: a => a > 0 ? parseInt(a) : 0,

        __fixunssfti: a => a > 0 ? parseInt(a) : 0,
        __fixunsdfti: a => a > 0 ? parseInt(a) : 0,
        __fixunstfti: a => a > 0 ? parseInt(a) : 0,
        __fixunsxfti: a => a > 0 ? parseInt(a) : 0,

        __floatsisf: a => a,
        __floatsidf: a => a,
        __floatsitf: a => a,
        __floatsixf: a => a,

        __floatdisf: a => a,
        __floatdidf: a => a,
        __floatditf: a => a,
        __floatdixf: a => a,

        __cmpsf2: (a, b) => a > b ? 1 : a < b ? -1 : 0,
        __cmpdf2: (a, b) => a > b ? 1 : a < b ? -1 : 0,
        __cmptf2: (a, b) => a > b ? 1 : a < b ? -1 : 0,
        
        __unordsf2: (a, b) => isNaN(a) || isNaN(b) ? 1 : 0,
        __unorddf2: (a, b) => isNaN(a) || isNaN(b) ? 1 : 0,
        __unordtf2: (a, b) => isNaN(a) || isNaN(b) ? 1 : 0,

        __eqsf2: (a, b) => a == b ? 0 : 1,
        __eqdf2: (a, b) => a == b ? 0 : 1,
        __eqtf2: (a, b) => a == b ? 0 : 1,

        __nesf2: (a, b) => !(a == b),
        __nedf2: (a, b) => !(a == b),
        __netf2: (a, b) => !(a == b),

        __gesf2: (a, b) => a >= b ? 1 : -1,
        __gedf2: (a, b) => a >= b ? 1 : -1,
        __getf2: (a, b) => a >= b ? 1 : -1,

        __ltsf2: (a, b) => a < b ? -1 : 1,
        __ltdf2: (a, b) => a < b ? -1 : 1,
        __lttf2: (a, b) => a < b ? -1 : 1,

        __lesf2: (a, b) => a <= b ? -1 : 1,
        __ledf2: (a, b) => a <= b ? -1 : 1,
        __letf2: (a, b) => a <= b ? -1 : 1,

        __gtsf2: (a, b) => a > b ? 1 : -1,
        __gtdf2: (a, b) => a > b ? 1 : -1,
        __gttf2: (a, b) => a > b ? 1 : -1,

        __floatunsitf: a => a,

        _start() {},
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
