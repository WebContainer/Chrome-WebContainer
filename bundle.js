const {print, open, read} = wlibc

print("WebContainer::Begin")

__GLOBAL__.window = {}

const {TextEncoder, TextDecoder} = require('text-encoding-shim')
const hexdump = require('hexdump-js')

const Utf8ArrayToStr = require('./src/Utf8ArrayToStr')

const memory = new WebAssembly.Memory({initial: 2})
const buffer = new Uint8Array(memory.buffer)

const STACK_BEGIN = 10000

// The stack/heap seaparator exists here.
// The stack grows downwards to zero.
// The heap grows upward to te page break.
buffer[1] = STACK_BEGIN

// Allocate free space on the heap.
let malloc_offset = STACK_BEGIN + 1
function malloc(s) {
    const next = malloc_offset
    malloc_offset = malloc_offset + s
    return next
}

const imports = {
    env: {
        memory,
        close: (fd) => {
            return wlibc.close(fd)
        },
        read: (fd, ptr, len) => {
            const buf = wlibc.read(fd, len)
            const bufa = new Uint8Array(buf)
            
            for(let i=0; i < bufa.length; i++) {
                buffer[ptr + i] = bufa[i]
            }

            return bufa.length
        },
        open: (pathPtr) => {
            let s = ""
            let i = pathPtr
            while(buffer[i] !== 0) {
                s += String.fromCharCode(buffer[i])
                i++
            }
            return wlibc.open(s)
        },
        print: (arg) => {
            let s = ""
            let i = arg
            while(buffer[i]) {
                s += String.fromCharCode(buffer[i])
                i++
            }
            wlibc.print(s)
        },
        malloc,
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
    print("WebContainer::ExitCode = " + r.instance.exports.main(argc, st))
    print("WebContainer::End")
})
.catch(e => print("ERROR" + e.message ))
