package io.github.christmasstar

import io.github.christmasstar.protocol.BytePort
import io.github.christmasstar.protocol.CsfxBuilder
import io.github.christmasstar.protocol.StarState
import io.github.christmasstar.protocol.StreamFrame
import io.github.christmasstar.protocol.XmodemSender
import io.github.christmasstar.protocol.crc16Xmodem
import io.github.christmasstar.star.StarLayout
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File

class ProtocolTest {

    @Test
    fun crc16CheckValue() {
        assertEquals(0x31C3, crc16Xmodem("123456789".toByteArray()))
    }

    @Test
    fun csfxMatchesPythonTool() {
        // Same input as the fixture made with sw/tools/make_effect.py.
        val red = IntArray(StarLayout.FACE_LEDS) { 0xE6140A }
        val blue = IntArray(StarLayout.FACE_LEDS) { 0x0A28FA }
        val tmp = File.createTempFile("csfx", ".frames")
        val bytes = CsfxBuilder.encode("fixture", true, listOf(red to 100, blue to 40), tmp)
        val expected = javaClass.getResourceAsStream("/fixture.csfx")!!.readBytes()
        assertArrayEquals(expected, bytes)
    }

    @Test
    fun csfxBackCopiesFront() {
        val front = IntArray(StarLayout.FACE_LEDS) { it * 3 }
        val bytes = CsfxBuilder.encode("x", false, listOf(front to 40), File.createTempFile("csfx", ".f"))
        val frame = bytes.copyOfRange(CsfxBuilder.HEADER_SIZE + 2, bytes.size)
        for (i in 0 until StarLayout.LED_COUNT) {
            val src = if (i < 66) i else StarLayout.partner[i]
            assertEquals((src * 3) and 0xFF, frame[3 * i + 2].toInt() and 0xFF)
        }
    }

    @Test
    fun streamFrameLayout() {
        val front = IntArray(StarLayout.FACE_LEDS) { 0xFF0000 }
        val f = StreamFrame.encodeFront(front)
        assertEquals(4 + 2 * 66 + 2, f.size)
        assertEquals(0xA5, f[0].toInt() and 0xFF)
        assertEquals(0x5A, f[1].toInt())
        assertEquals(StreamFrame.FLAG_BACK_COPY, f[2].toInt())
        assertEquals(66, f[3].toInt())
        // Pure red in RGB565 = 0xF800, little endian.
        assertEquals(0x00, f[4].toInt() and 0xFF)
        assertEquals(0xF8, f[5].toInt() and 0xFF)
        val crc = crc16Xmodem(f, 2, 2 + 132)
        assertEquals(crc shr 8, f[136].toInt() and 0xFF)
        assertEquals(crc and 0xFF, f[137].toInt() and 0xFF)

        val end = StreamFrame.end()
        assertEquals(6, end.size)
        assertEquals(StreamFrame.FLAG_END, end[2].toInt() and 0xFF)
    }

    @Test
    fun rgb565() {
        assertEquals(0xFFFF, StreamFrame.rgb565(0xFFFFFF))
        assertEquals(0x07E0, StreamFrame.rgb565(0x00FF00))
        assertEquals(0x001F, StreamFrame.rgb565(0x0000FF))
    }

    @Test
    fun parseState() {
        val s = StarState.parse(
            "state effect=golden bright=128 shake=1 speed=150 fps=51 missing=2 cap=655040 flash=my video frames=38",
        )!!
        assertEquals("golden", s.effect)
        assertEquals(128, s.brightness)
        assertTrue(s.shake)
        assertEquals(150, s.speed)
        assertEquals(655040L, s.capacityFrames)
        assertEquals("my video", s.flashName)
        assertEquals(38, s.flashFrames)

        val empty = StarState.parse("state effect=manual bright=5 shake=0 speed=100 fps=50 missing=0 cap=1 flash=- frames=0")!!
        assertNull(empty.flashName)
        assertNull(StarState.parse("nonsense"))
    }

    /** A receiver that behaves like the star's XMODEM-1K receiver. */
    private class FakeStar(private val corruptFirst: Boolean) : BytePort {
        val toSender = Channel<Int>(Channel.UNLIMITED)
        val received = ByteArrayOutputStream()
        private var expected = 1
        private var corrupted = false
        var done = false

        init {
            toSender.trySend('C'.code)
        }

        override suspend fun write(data: ByteArray) {
            if (data.size == 1 && data[0] == XmodemSender.EOT) {
                done = true
                toSender.send(6)
                return
            }
            if (data.size == 2 && data[0] == XmodemSender.CAN) return
            assertEquals(XmodemSender.STX, data[0])
            val blk = data[1].toInt() and 0xFF
            assertEquals(0xFF, blk xor (data[2].toInt() and 0xFF))
            val payload = data.copyOfRange(3, 3 + 1024)
            val crc = ((data[1027].toInt() and 0xFF) shl 8) or (data[1028].toInt() and 0xFF)
            if (corruptFirst && !corrupted) {
                corrupted = true
                toSender.send(0x15) // NAK
                return
            }
            assertEquals(crc16Xmodem(payload), crc)
            assertEquals(expected and 0xFF, blk)
            received.write(payload)
            expected++
            toSender.send(6) // ACK
        }

        override suspend fun read(timeoutMs: Long): Int =
            withTimeoutOrNull(timeoutMs) { toSender.receive() } ?: -1
    }

    @Test
    fun xmodemSendsAllBlocksAndRetries() = runBlocking {
        val data = ByteArray(2500) { (it * 7).toByte() }
        val star = FakeStar(corruptFirst = true)
        var last = 0L
        XmodemSender(star).send(data.inputStream(), data.size.toLong(), { last = it })
        assertTrue(star.done)
        assertEquals(2500L, last)
        val got = star.received.toByteArray()
        assertEquals(3 * 1024, got.size)
        assertArrayEquals(data, got.copyOf(2500))
        // Padding is 0x1A.
        assertTrue(got.copyOfRange(2500, got.size).all { it == XmodemSender.PAD })
    }
}
