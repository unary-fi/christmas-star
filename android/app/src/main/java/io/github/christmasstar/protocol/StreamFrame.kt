package io.github.christmasstar.protocol

import io.github.christmasstar.star.StarLayout

/**
 * Binary live frames for the star's `stream` command
 * (see sw/src/transfer/stream_frame.h).
 *
 * A5 5A | flags | n | n x RGB565 (little endian) | CRC-16/XMODEM (big endian)
 */
object StreamFrame {
    const val ACK = 0x06
    const val NAK = 0x15
    const val FLAG_BACK_COPY = 0x01
    const val FLAG_END = 0x80

    fun rgb565(rgb: Int): Int {
        val r = (rgb shr 16) and 0xFF
        val g = (rgb shr 8) and 0xFF
        val b = rgb and 0xFF
        return ((r shr 3) shl 11) or ((g shr 2) shl 5) or (b shr 3)
    }

    /**
     * @param front colours (0xRRGGBB) of the 66 front LEDs; the star copies
     *        each one to its back partner at the same position.
     */
    fun encodeFront(front: IntArray): ByteArray = encode(front, StarLayout.FACE_LEDS, FLAG_BACK_COPY)

    /** End-of-stream frame. */
    fun end(): ByteArray = encode(IntArray(0), 0, FLAG_END)

    private fun encode(colors: IntArray, n: Int, flags: Int): ByteArray {
        val out = ByteArray(4 + 2 * n + 2)
        out[0] = 0xA5.toByte()
        out[1] = 0x5A
        out[2] = flags.toByte()
        out[3] = n.toByte()
        for (i in 0 until n) {
            val v = rgb565(colors[i])
            out[4 + 2 * i] = (v and 0xFF).toByte()
            out[5 + 2 * i] = (v shr 8).toByte()
        }
        val crc = crc16Xmodem(out, 2, 2 + 2 * n)
        out[4 + 2 * n] = (crc shr 8).toByte()
        out[5 + 2 * n] = (crc and 0xFF).toByte()
        return out
    }
}
