print("BEGIN")

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
print('fd=' + fd)
const buf = read(fd, 9999999999)
print(buf)

const memory = new WebAssembly.Memory({initial: 1});
const imports = {
    env: {
        memory,
        read() {print("READ"); return 0;},
        open() {print("OPEN"); return 0;},
    }
}

new Uint32Array(memory.buffer)[1] = 10000

const o = WebAssembly
.instantiate(buf, imports)
.then(r => r.instance.exports.main())
.catch(e => print("ERROR" + e.message ))
