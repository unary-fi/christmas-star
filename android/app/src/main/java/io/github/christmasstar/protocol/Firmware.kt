package io.github.christmasstar.protocol

/**
 * The star firmware bundled with the app (assets/firmware/star-firmware.bin,
 * a signed MCUboot image) and version handling for over-the-air updates.
 *
 * A star can be updated from the app when it runs firmware 1.3.0 or newer
 * (the first with the MCUboot bootloader and the `fw` commands) and its
 * board has the BOOT0 pull-down.
 */
object Firmware {
    const val ASSET = "firmware/star-firmware.bin"
    private const val IMAGE_MAGIC = 0x96F3B83D.toInt()

    /** The oldest firmware that can take an update over Bluetooth. */
    val FIRST_UPDATABLE = Version(1, 3, 0)

    data class Version(val major: Int, val minor: Int, val patch: Int) : Comparable<Version> {
        override fun compareTo(other: Version) =
            compareValuesBy(this, other, Version::major, Version::minor, Version::patch)

        override fun toString() = "$major.$minor.$patch"

        companion object {
            /** "1.4.0" (anything after the third number is ignored), or null. */
            fun parse(text: String): Version? {
                val parts = text.trim().split('.', '+', '-').mapNotNull { it.toIntOrNull() }
                return if (parts.size >= 3) Version(parts[0], parts[1], parts[2]) else null
            }
        }
    }

    /** Version of a signed image (from its MCUboot header), or null if it is not one. */
    fun imageVersion(image: ByteArray): Version? {
        if (image.size < 32 || le32(image, 0) != IMAGE_MAGIC) return null
        val major = image[20].toInt() and 0xFF
        val minor = image[21].toInt() and 0xFF
        val patch = (image[22].toInt() and 0xFF) or ((image[23].toInt() and 0xFF) shl 8)
        return Version(major, minor, patch)
    }

    /** True when [bundled] should be offered to a star running [starFw]. */
    fun updateAvailable(starFw: String, bundled: Version?): Boolean {
        val star = Version.parse(starFw) ?: return false
        return bundled != null && star >= FIRST_UPDATABLE && bundled > star
    }

    private fun le32(b: ByteArray, at: Int) = (b[at].toInt() and 0xFF) or ((b[at + 1].toInt() and 0xFF) shl 8) or
        ((b[at + 2].toInt() and 0xFF) shl 16) or ((b[at + 3].toInt() and 0xFF) shl 24)
}
