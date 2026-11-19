package io.github.christmasstar

import io.github.christmasstar.protocol.Firmware
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class FirmwareTest {
    private fun header(major: Int, minor: Int, patch: Int) = ByteArray(64).also {
        val magic = 0x96F3B83D.toInt()
        for (i in 0 until 4) it[i] = (magic shr (8 * i)).toByte()
        it[20] = major.toByte()
        it[21] = minor.toByte()
        it[22] = patch.toByte()
    }

    @Test
    fun readsImageVersion() {
        assertEquals(Firmware.Version(1, 5, 1), Firmware.imageVersion(header(1, 5, 1)))
        assertNull(Firmware.imageVersion("CSFX".toByteArray() + ByteArray(60)))
        assertNull(Firmware.imageVersion(ByteArray(10)))
    }

    @Test
    fun parsesAndComparesVersions() {
        assertEquals(Firmware.Version(1, 4, 0), Firmware.Version.parse("1.4.0"))
        assertEquals(Firmware.Version(1, 3, 2), Firmware.Version.parse("1.3.2+7"))
        assertNull(Firmware.Version.parse("1.2"))
        assertTrue(Firmware.Version(1, 10, 0) > Firmware.Version(1, 9, 9))
    }

    @Test
    fun offersUpdatesOnlyWhenPossibleAndNewer() {
        val bundled = Firmware.Version(1, 5, 1)
        assertTrue(Firmware.updateAvailable("1.4.0", bundled))
        assertFalse(Firmware.updateAvailable("1.5.1", bundled))
        assertFalse(Firmware.updateAvailable("1.6.0", bundled))
        // Before 1.3.0 there is no bootloader: the J-Link is needed once.
        assertFalse(Firmware.updateAvailable("1.2", bundled))
        assertFalse(Firmware.updateAvailable("1.2.0", bundled))
        assertFalse(Firmware.updateAvailable("", bundled))
        assertFalse(Firmware.updateAvailable("1.4.0", null))
    }

    @Test
    fun bundledFirmwareIsASignedImage() {
        val file = File("src/main/assets/${Firmware.ASSET}")
        assertTrue("bundled firmware present", file.isFile)
        val v = Firmware.imageVersion(file.readBytes())
        assertTrue("bundled firmware is a signed image", v != null && v >= Firmware.FIRST_UPDATABLE)
    }
}
