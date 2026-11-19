package io.github.christmasstar.protocol

import io.github.christmasstar.star.StarLayout
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.SequenceInputStream
import java.util.zip.CRC32

/**
 * Christmas Star effect file (.csfx), identical to sw/tools/make_effect.py:
 *
 * header (32 bytes, little endian): "CSFX", version 1, led_count 132,
 *   flags (bit 0 = loop), name[16], frame_count, crc32 of all frame bytes
 * frame: u16 duration_ms + 132 x RGB in logical LED order
 *
 * Frames are given as the 66 front colours; the back LEDs copy their front
 * partner (same picture on both faces).
 */
class CsfxBuilder(private val name: String, private val loop: Boolean, private val framesFile: File) {
    private val out = BufferedOutputStream(FileOutputStream(framesFile))
    private val crc = CRC32()
    private val frame = ByteArray(FRAME_SIZE)
    var frameCount = 0
        private set

    fun addFrame(front: IntArray, durationMs: Int) {
        val d = durationMs.coerceIn(1, 65535)
        frame[0] = (d and 0xFF).toByte()
        frame[1] = (d shr 8).toByte()
        for (i in 0 until StarLayout.LED_COUNT) {
            val front_i = if (i < StarLayout.FACE_LEDS) i else StarLayout.partner[i]
            val c = front[front_i]
            frame[2 + 3 * i] = (c shr 16).toByte()
            frame[3 + 3 * i] = (c shr 8).toByte()
            frame[4 + 3 * i] = c.toByte()
        }
        out.write(frame)
        crc.update(frame)
        frameCount++
    }

    /** Finish writing; returns the complete file as a stream and its size. */
    fun finish(): Pair<InputStream, Long> {
        out.close()
        val header = header(name, loop, frameCount, crc.value)
        val size = header.size + framesFile.length()
        return SequenceInputStream(header.inputStream(), framesFile.inputStream()) to size
    }

    companion object {
        const val HEADER_SIZE = 32
        const val FRAME_SIZE = 2 + 3 * StarLayout.LED_COUNT

        fun header(name: String, loop: Boolean, frames: Int, crc32: Long): ByteArray {
            val h = ByteArray(HEADER_SIZE)
            "CSFX".toByteArray(Charsets.US_ASCII).copyInto(h, 0)
            h[4] = 1
            h[5] = StarLayout.LED_COUNT.toByte()
            h[6] = if (loop) 1 else 0
            h[7] = 0
            val n = name.toByteArray(Charsets.UTF_8)
            n.copyInto(h, 8, 0, minOf(16, n.size))
            putLe32(h, 24, frames.toLong())
            putLe32(h, 28, crc32)
            return h
        }

        private fun putLe32(b: ByteArray, at: Int, v: Long) {
            for (i in 0 until 4) b[at + i] = (v shr (8 * i)).toByte()
        }

        /** Whole file in memory (small effects such as a single photo). */
        fun encode(name: String, loop: Boolean, frames: List<Pair<IntArray, Int>>, tmp: File): ByteArray {
            val b = CsfxBuilder(name, loop, tmp)
            frames.forEach { (front, d) -> b.addFrame(front, d) }
            val (stream, _) = b.finish()
            return stream.use { it.readBytes() }.also { tmp.delete() }
        }
    }
}
