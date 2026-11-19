package io.github.christmasstar.protocol

/** CRC-16/XMODEM (poly 0x1021, init 0), as used by the star. */
fun crc16Xmodem(data: ByteArray, offset: Int = 0, length: Int = data.size - offset): Int {
    var crc = 0
    for (i in offset until offset + length) {
        crc = crc xor ((data[i].toInt() and 0xFF) shl 8)
        repeat(8) {
            crc = if (crc and 0x8000 != 0) (crc shl 1) xor 0x1021 else crc shl 1
            crc = crc and 0xFFFF
        }
    }
    return crc
}
