package io.github.christmasstar

import io.github.christmasstar.protocol.EffectCatalog
import io.github.christmasstar.protocol.PreviewClip
import io.github.christmasstar.protocol.StreamFrame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class EffectCatalogTest {
    @Test
    fun parsesEffectList() {
        val list = EffectCatalog.parseList(
            "fx golden - Golden Star|Warm gold, gently shimmering\r\n" +
                "fx liquidgold t Liquid Gold|Tilt the star and the gold flows\r\n" +
                "fx hearthfrost 2 Hearth & Frost|Warm and frosty faces\r\n" +
                "star:~$ ",
        )
        assertEquals(listOf("golden", "liquidgold", "hearthfrost"), list.map { it.id })
        assertEquals("Hearth & Frost", list[2].title)
        assertEquals("Warm and frosty faces", list[2].subtitle)
        assertTrue(list[1].tilt)
        assertFalse(list[1].twoFaces)
        assertTrue(list[2].twoFaces)
    }

    @Test
    fun decodesPreviewClip() {
        // Two frames in the star's stream format, surrounded by the text lines.
        val f1 = StreamFrame.encodeFront(IntArray(66) { 0xFF0000 })
        val f2 = StreamFrame.encodeFront(IntArray(66) { 0x0000FF })
        val raw = "preview golden frames=2 step=80 leds=66\r\n".toByteArray() + f1 + f2 +
            "\r\npreview end\r\nstar:~$ ".toByteArray()
        val clip = PreviewClip.decode(raw)
        assertNotNull(clip)
        clip!!
        assertEquals(80, clip.stepMs)
        assertEquals(2, clip.frames.size)
        assertEquals(66, clip.frames[0].size)
        assertEquals(0xFF0000, clip.frames[0][0])
        assertEquals(0x0000FF, clip.frames[1][65])
        assertEquals(0x0000FF, clip.frameAt(80)[0])
        assertEquals(0xFF0000, clip.frameAt(160)[0])
    }

    @Test
    fun rejectsDamagedFrames() {
        val f = StreamFrame.encodeFront(IntArray(66) { 0x00FF00 })
        f[10] = (f[10].toInt() xor 0x40).toByte()
        assertNull(PreviewClip.decode(f))
    }
}
