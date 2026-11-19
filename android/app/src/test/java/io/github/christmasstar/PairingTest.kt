package io.github.christmasstar

import io.github.christmasstar.protocol.Hello
import io.github.christmasstar.protocol.Pairing
import io.github.christmasstar.protocol.StarState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PairingTest {
    @Test
    fun authVectorSharedWithFirmware() {
        // Same vector as sw/tests/unit/src/test_pair.c and pc/tests/test_studio.py.
        val key = ByteArray(16) { it.toByte() }
        val nonce = ByteArray(16) { (0x10 + it).toByte() }
        assertEquals(
            "8b9b4fe50baada231361d9491a71d3ef7ae699bc95c7366a34c5784d99370d9a",
            Pairing.authMac(key, nonce),
        )
    }

    @Test
    fun parsesHello() {
        val h = Hello.parse(
            "ok proto=2 leds=132 fw=1.2 owner=paired pairing=open access=none " +
                "nonce=00112233445566778899aabbccddeeff name=Living room",
        )
        assertNotNull(h)
        h!!
        assertEquals(2, h.proto)
        assertTrue(h.paired)
        assertTrue(h.pairingOpen)
        assertEquals("none", h.access)
        assertEquals(16, h.nonce.size)
        assertEquals("Living room", h.name)
        assertEquals(1, Hello.parse("ok proto=1 leds=132 fw=1.1")!!.proto)
        assertNull(Hello.parse("locked"))
    }

    @Test
    fun keysAndNames() {
        assertNotNull(Pairing.parseKey("00112233445566778899AABBCCDDEEFF"))
        assertNull(Pairing.parseKey("0011"))
        assertNull(Pairing.parseKey("00000000000000000000000000000000"))
        assertNull(Pairing.parseKey("zz112233445566778899aabbccddeeff"))
        assertEquals(16, Pairing.newKey().size)
        assertTrue(Pairing.validName("Living room"))
        assertFalse(Pairing.validName(""))
        assertFalse(Pairing.validName("Seventeen chars!!"))
        assertFalse(Pairing.validName("Jouluähti"))
    }

    @Test
    fun parsesStateWithOwnerAndName() {
        val s = StarState.parse(
            "state effect=flash bright=128 shake=1 speed=100 fps=50 missing=1 cap=655040 " +
                "flash=My video frames=250 owner=paired name=Living room",
        )!!
        assertEquals("My video", s.flashName)
        assertEquals(250, s.flashFrames)
        assertEquals("paired", s.owner)
        assertEquals("Living room", s.name)
        // Older firmware without owner/name.
        val old = StarState.parse("state effect=golden bright=1 shake=0 speed=100 fps=50 missing=0 cap=1 flash=- frames=0")!!
        assertNull(old.flashName)
        assertEquals("", old.name)
    }
}
