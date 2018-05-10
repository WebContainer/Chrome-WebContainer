const {print, open, read} = wlibc

print("WebContainer::Begin")

function Utf8ArrayToStr(array) {
    var out, i, len, c;
    var char2, char3;
  
    out = "";
    len = array.length;
    i = 0;
    while (i < len) {
      c = array[i++];
      switch (c >> 4)
      { 
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
          // 0xxxxxxx
          out += String.fromCharCode(c);
          break;
        case 12: case 13:
          // 110x xxxx   10xx xxxx
          char2 = array[i++];
          out += String.fromCharCode(((c & 0x1F) << 6) | (char2 & 0x3F));
          break;
        case 14:
          // 1110 xxxx  10xx xxxx  10xx xxxx
          char2 = array[i++];
          char3 = array[i++];
          out += String.fromCharCode(((c & 0x0F) << 12) |
                                     ((char2 & 0x3F) << 6) |
                                     ((char3 & 0x3F) << 0));
          break;
      }
    }    
    return out;
  }
  
const fd = open("test.wasm")
const buf = read(fd, 9999999999)

const memory = new WebAssembly.Memory({initial: 2});
const buffer = new Uint8Array(memory.buffer);

new Uint32Array(memory.buffer)[1] = 10000
let malloc_offset = 10001

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
        malloc(s) {
            const next = malloc_offset
            malloc_offset = malloc_offset + s
            return next
        },
        _start() {},
    },
}

const o = WebAssembly
.instantiate(buf, imports)
.then(r => {
    print("WebContainer::ExitCode = " + r.instance.exports.main())
    print("WebContainer::End")
})
.catch(e => print("ERROR" + e.message ))
